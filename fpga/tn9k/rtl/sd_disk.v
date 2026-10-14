// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The disk port of the sim09 machine ($E040-$E046) on a microSD card in SPI mode.
//
// The guest asks for one 256 byte sector (LSN, 24 bits) of drive 0 or 1, to or from a buffer in its
// memory (DMA, here: byte accesses to the PSRAM).  The card is read in blocks of 512 bytes, so a sector
// is one half of a block; a write is a read-modify-write of the block, which is kept in a 512 byte
// buffer (the last block used stays valid, which makes the second sector of a block cheap).
//
//   drive 0:  card block lsn/2                     (a disk image is written with dd to the card, from 0)
//   drive 1:  card block lsn/2 + DRIVE1_BLK        (default 128 MB into the card)
//
// The card is set up after the reset (CMD0, CMD8, ACMD41, CMD58 -> SDSC or SDHC/SDXC, 512 byte blocks).
// A command that comes while it is not ready waits for it; if the card is missing, the result is $FF
// (the same error the simulator gives for a drive without an image), and the next command tries again.

module sd_disk #(
    parameter [23:0] DRIVE1_BLK = 24'h040000
) (
    input  wire        clk,
    input  wire        rst,
    // command
    input  wire        go,              // one clk pulse
    input  wire        wr,              // 1: write the sector to the card, 0: read it
    input  wire [7:0]  drive,
    input  wire [23:0] lsn,
    input  wire [15:0] buf_addr,
    output wire        busy,
    output reg  [7:0]  result,          // 0 = good, $FF = error
    // PSRAM master (byte accesses)
    output reg         m_rd,
    output reg         m_wr,
    output reg  [21:0] m_addr,
    output reg  [7:0]  m_din,
    input  wire [15:0] m_dout,
    input  wire        m_busy,
    // microSD
    output reg         sd_clk,
    output reg         sd_mosi,
    input  wire        sd_miso,
    output reg         sd_cs,
    // state for the LEDs
    output reg         ready,
    output reg         fail,
    output wire        active,
    output wire [5:0]  dbg_st
);

    // ---------------------------------------------------------------- SPI byte engine (mode 0)
    localparam SLOW_HALF = 128;         // 67.5 MHz / 256 = 264 kHz, for the card setup
    localparam FAST_HALF = 3;           // 67.5 MHz / 6 = 11.25 MHz

    reg        fast;
    reg        spi_go;
    reg  [7:0] spi_tx;
    reg  [7:0] spi_rx;
    reg        spi_busy;
    reg        spi_done;
    reg  [7:0] hcnt;
    reg  [3:0] bcnt;
    reg  [7:0] sh_out, sh_in;

    always @(posedge clk) begin
        spi_done <= 1'b0;
        if (rst) begin
            spi_busy <= 1'b0;
            sd_clk   <= 1'b0;
            sd_mosi  <= 1'b1;
        end else if (!spi_busy) begin
            sd_clk <= 1'b0;
            if (spi_go) begin
                sh_out   <= {spi_tx[6:0], 1'b1};
                sd_mosi  <= spi_tx[7];
                bcnt     <= 4'd0;
                hcnt     <= 8'd0;
                spi_busy <= 1'b1;
            end else sd_mosi <= 1'b1;
        end else if (hcnt != (fast ? FAST_HALF - 1 : SLOW_HALF - 1)) begin
            hcnt <= hcnt + 1'b1;
        end else begin
            hcnt <= 8'd0;
            if (!sd_clk) begin                      // rising edge: the card takes MOSI, we take MISO
                sd_clk <= 1'b1;
                sh_in  <= {sh_in[6:0], sd_miso};
            end else begin                          // falling edge: the next bit
                sd_clk  <= 1'b0;
                bcnt    <= bcnt + 1'b1;
                sd_mosi <= sh_out[7];
                sh_out  <= {sh_out[6:0], 1'b1};
                if (bcnt == 4'd7) begin
                    spi_busy <= 1'b0;
                    spi_done <= 1'b1;
                    spi_rx   <= sh_in;
                end
            end
        end
    end

    // ---------------------------------------------------------------- the 512 byte block buffer
    reg [7:0] bram [0:511];
    reg [8:0] b_waddr, b_raddr;
    reg [7:0] b_wdata;
    reg       b_we;
    reg [7:0] b_q;
    always @(posedge clk) begin
        if (b_we) bram[b_waddr] <= b_wdata;
        b_q <= bram[b_raddr];
    end

    // ---------------------------------------------------------------- the sequencer
    localparam [5:0]
        S_RST     = 6'd0,
        S_PWR     = 6'd1,
        S_DUMMY   = 6'd2,
        S_CMD0    = 6'd3,
        S_CMD0R   = 6'd4,
        S_CMD8    = 6'd5,
        S_CMD8R   = 6'd6,
        S_ACMD55  = 6'd7,
        S_ACMD41  = 6'd8,
        S_ACMD41R = 6'd9,
        S_CMD58   = 6'd10,
        S_CMD58R  = 6'd11,
        S_CMD16   = 6'd12,
        S_FAST    = 6'd13,
        S_IDLE    = 6'd14,
        S_FAILED  = 6'd15,
        S_WAIT    = 6'd16,     // a byte is on its way, then go to ret
        S_CTX     = 6'd17,     // command bytes
        S_CR1     = 6'd18,     // wait for R1
        S_CR1B    = 6'd19,
        S_R4      = 6'd20,     // four more bytes (R3, R7)
        S_R4B     = 6'd21,
        S_SKIP    = 6'd22,     // n bytes to throw away
        S_BLK     = 6'd23,     // get the block into the buffer
        S_RD1     = 6'd24,
        S_TOKEN   = 6'd25,
        S_TOKENB  = 6'd26,
        S_RDATA   = 6'd27,
        S_RDATAB  = 6'd28,
        S_RDONE   = 6'd29,
        S_AFTER   = 6'd30,     // the block is in the buffer
        S_CPO     = 6'd31,     // buffer -> PSRAM, one byte
        S_CPO1    = 6'd32,
        S_CPO2    = 6'd33,
        S_CPO3    = 6'd34,
        S_CPO4    = 6'd35,
        S_CPI     = 6'd36,     // PSRAM -> buffer, one byte
        S_CPI1    = 6'd37,
        S_CPI2    = 6'd38,
        S_WR1     = 6'd39,
        S_WR2     = 6'd40,
        S_WTOKEN  = 6'd41,
        S_WDATA0  = 6'd42,
        S_WDATA   = 6'd43,
        S_WDATA1  = 6'd44,
        S_WDATA2  = 6'd45,
        S_WDATA3  = 6'd46,
        S_WRESP   = 6'd47,
        S_WRESPB  = 6'd48,
        S_WBUSY   = 6'd49,
        S_WBUSYB  = 6'd50,
        S_END     = 6'd51,
        S_ENDB    = 6'd52,
        S_CMDERR  = 6'd53,
        S_PRE     = 6'd54;


    reg [5:0]  st, ret, cmd_next, skip_next;
    // delays and time-outs: a counter that counts (or is cleared) one clock after the request, in two parts (a 21 bit adder is too slow)
    reg        c_inc, c_clr;
    reg [7:0]  cnt_lo;
    reg [12:0] cnt_hi;
    wire [20:0] cnt = {cnt_hi, cnt_lo};

    reg [9:0]  idx;                     // byte index
    reg [4:0]  tries;
    reg [3:0]  ctry;
    reg [47:0] cbuf;
    reg [2:0]  cstep;
    reg [31:0] rword;
    reg [1:0]  rbytes;
    reg [1:0]  skipn;
    reg        sdv2, ccs;
    reg        pend;
    reg        exec;
    reg        cvalid;
    reg [23:0] cblk;
    reg        hit_q, idx_255, idx_511, idx_10;     // comparisons, registered: their inputs are steady for many clocks
    reg [23:0] blk;
    reg        half;
    reg        wr_r;
    reg [15:0] buf_r;
    reg        bad_drive;

    always @(posedge clk) begin
        hit_q   <= cvalid && (cblk == blk);
        idx_255 <= (idx[7:0] == 8'hff);
        idx_511 <= (idx == 10'd511);
        idx_10  <= (idx == 10'd10);
    end

    assign dbg_st = st;
    assign busy   = pend | exec;
    assign active = exec;

    // what the byte just received is, decided one clock after it has arrived
    reg rx_fe, rx_ff, rx_resp, rx_r1, rx_00, rx_01, rx_b2;
    reg r1_ok, r1_idle, r1_ill;                 // the R1 of the last command
    always @(posedge clk) begin
        rx_fe   <= (spi_rx == 8'hfe);
        rx_ff   <= (spi_rx == 8'hff);
        rx_resp <= (spi_rx[4:0] == 5'b00101);
        rx_r1   <= ~spi_rx[7];
        rx_00   <= (spi_rx == 8'h00);
        rx_01   <= (spi_rx == 8'h01);
        rx_b2   <= spi_rx[2];
    end

    wire [31:0] blk_arg = ccs ? {8'd0, blk} : {blk[22:0], 9'd0};

    // the state that follows a byte transfer is a function of the state that started it (so no register has to be loaded for it)
    reg [5:0] caller;
    function [5:0] retmap;
        input [5:0] c;
        case (c)
            S_DUMMY:  retmap = S_DUMMY;
            S_FAST:   retmap = S_IDLE;
            S_CTX:    retmap = S_CTX;
            S_CR1:    retmap = S_CR1B;
            S_R4:     retmap = S_R4B;
            S_SKIP:   retmap = S_SKIP;
            S_RDONE:  retmap = S_AFTER;
            S_TOKEN:  retmap = S_TOKENB;
            S_RDATA:  retmap = S_RDATAB;
            S_WR2:    retmap = S_WTOKEN;
            S_WTOKEN: retmap = S_WDATA0;
            S_WDATA2: retmap = S_WDATA3;
            S_WRESP:  retmap = S_WRESPB;
            S_WBUSY:  retmap = S_WBUSYB;
            default:  retmap = S_ENDB;                  // S_END, S_CMDERR
        endcase
    endfunction

    task xfer(input [7:0] b, input [5:0] nxt);
        begin
            spi_tx <= b;
            spi_go <= 1'b1;
            st     <= S_WAIT;
        end
    endtask

    task send_cmd(input [5:0] c, input [31:0] arg, input [7:0] crc, input [5:0] nxt);
        begin
            cbuf     <= {2'b01, c, arg, crc};
            cstep    <= 3'd0;
            tries    <= 5'd0;
            cmd_next <= nxt;
            st       <= S_CTX;
        end
    endtask

    always @(posedge clk) begin
        if (c_clr) begin cnt_lo <= 8'd0; cnt_hi <= 13'd0; end
        else if (c_inc) begin
            cnt_lo <= cnt_lo + 1'b1;
            if (cnt_lo == 8'hff) cnt_hi <= cnt_hi + 1'b1;
        end
    end

    always @(posedge clk) begin
        m_addr  <= {6'd0, buf_r + {8'd0, idx[7:0]}};             // no enables: used with the m_rd / m_wr pulses
        m_din   <= b_q;
        b_waddr <= (st == S_RDATAB) ? idx[8:0] : {half, idx[7:0]};          // no enable: they are used with b_we
        b_wdata <= (st == S_RDATAB) ? spi_rx : (m_addr[0] ? m_dout[15:8] : m_dout[7:0]);
        if (st != S_WAIT) caller <= st;
        spi_go <= 1'b0;
        c_inc  <= 1'b0;
        c_clr  <= 1'b0;
        m_rd   <= 1'b0;
        m_wr   <= 1'b0;
        b_we   <= 1'b0;
        if (go) begin
            pend      <= 1'b1;
            wr_r      <= wr;
            buf_r     <= buf_addr;
            blk       <= {1'b0, lsn[23:1]} + (drive[0] ? DRIVE1_BLK : 24'd0);
            half      <= lsn[0];
            bad_drive <= (drive > 8'd1);
        end
        if (rst) begin
            st     <= S_RST;
            ready  <= 1'b0;
            fail   <= 1'b0;
            sd_cs  <= 1'b1;
            fast   <= 1'b0;
            pend   <= 1'b0;
            exec   <= 1'b0;
            cvalid <= 1'b0;
            result <= 8'hff;
            c_clr  <= 1'b1;
        end else case (st)
        // ------------------------------------------------ card setup
        S_RST: begin
            sd_cs <= 1'b1; fast <= 1'b0; ready <= 1'b0; cvalid <= 1'b0;
            c_clr <= 1'b1; ctry <= 4'd0; st <= S_PWR;
        end
        S_PWR: begin                            // 4 ms for the card's supply
            c_inc <= 1'b1;
            if (cnt[18]) begin idx <= 10'd0; st <= S_DUMMY; end              // 2**18 clocks: 3.2 ms
        end
        S_DUMMY: begin                          // 80 clocks with CS high
            if (idx_10) st <= S_CMD0;
            else begin idx <= idx + 1'b1; xfer(8'hff, S_DUMMY); end
        end
        S_CMD0: begin
            sd_cs <= 1'b0;
            send_cmd(6'd0, 32'd0, 8'h95, S_CMD0R);
        end
        S_CMD0R: begin
            if (r1_idle) st <= S_CMD8;
            else if (ctry == 4'hf) begin fail <= 1'b1; sd_cs <= 1'b1; st <= S_FAILED; end   // no card
            else begin ctry <= ctry + 1'b1; st <= S_CMD0; end
        end
        S_CMD8: send_cmd(6'd8, 32'h000001aa, 8'h87, S_CMD8R);
        S_CMD8R: begin
            c_clr <= 1'b1;
            if (r1_ill) begin                   // illegal command: a version 1 card
                sdv2 <= 1'b0; st <= S_ACMD55;
            end else begin                      // 4 more bytes: the echo of voltage and pattern
                sdv2 <= 1'b1; rbytes <= 2'd0; cmd_next <= S_ACMD55; st <= S_R4;
            end
        end
        S_ACMD55: send_cmd(6'd55, 32'd0, 8'h65, S_ACMD41);
        S_ACMD41: send_cmd(6'd41, sdv2 ? 32'h40000000 : 32'd0, 8'h77, S_ACMD41R);
        S_ACMD41R: begin
            if (r1_ok) st <= sdv2 ? S_CMD58 : S_CMD16;
            else if (cnt[12]) begin fail <= 1'b1; sd_cs <= 1'b1; st <= S_FAILED; end
            else begin c_inc <= 1'b1; st <= S_ACMD55; end
        end
        S_CMD58: begin ccs <= 1'b0; send_cmd(6'd58, 32'd0, 8'hfd, S_CMD58R); end
        S_CMD58R: begin rbytes <= 2'd0; cmd_next <= S_FAST; st <= S_R4; end
        S_CMD16: send_cmd(6'd16, 32'd512, 8'h15, S_FAST);
        S_FAST: begin
            ccs  <= sdv2 ? rword[30] : 1'b0;    // CCS of the OCR of CMD58 (a version 1 card: byte addresses)
            fast <= 1'b1; sd_cs <= 1'b1; ready <= 1'b1; fail <= 1'b0;
            xfer(8'hff, S_IDLE);
        end
        S_FAILED: begin                         // no card: the command fails at once, and the setup starts again
            if (pend) begin result <= 8'hff; pend <= 1'b0; st <= S_RST; end
        end
        // ------------------------------------------------ one byte on the wire
        S_WAIT: if (spi_done) st <= retmap(caller);
        // ------------------------------------------------ one command: 6 bytes, then R1
        S_CTX: begin
            if (cstep == 3'd6) st <= S_CR1;
            else begin
                xfer(cbuf[47:40], S_CTX);
                cbuf  <= {cbuf[39:0], 8'hff};
                cstep <= cstep + 1'b1;
            end
        end
        S_CR1: begin
            tries <= tries + 1'b1;
            xfer(8'hff, S_CR1B);
        end
        S_CR1B: begin
            if (rx_r1) begin r1_ok <= rx_00; r1_idle <= rx_01; r1_ill <= rx_b2; st <= cmd_next; end
            else if (tries[4]) begin r1_ok <= 1'b0; r1_idle <= 1'b0; r1_ill <= 1'b1; st <= cmd_next; end
            else st <= S_CR1;
        end
        S_R4: xfer(8'hff, S_R4B);
        S_R4B: begin
            rword  <= {rword[23:0], spi_rx};
            rbytes <= rbytes + 1'b1;
            st     <= (rbytes == 2'd3) ? cmd_next : S_R4;
        end
        S_SKIP: begin
            if (skipn == 2'd0) st <= skip_next;
            else begin skipn <= skipn - 1'b1; xfer(8'hff, S_SKIP); end
        end
        // ------------------------------------------------ a sector
        S_IDLE: if (pend) begin
            exec <= 1'b1;
            st   <= S_PRE;
        end
        S_PRE: begin                            // a clock for hit_q to follow the new block number
            if (bad_drive) st <= S_CMDERR;
            else if (hit_q) st <= S_AFTER;
            else st <= S_BLK;
        end
        S_BLK: begin                            // CMD17: read a block into the buffer
            cvalid <= 1'b0;
            sd_cs  <= 1'b0;
            send_cmd(6'd17, blk_arg, 8'hff, S_RD1);
        end
        S_RD1: begin
            c_clr <= 1'b1;
            st  <= r1_ok ? S_TOKEN : S_CMDERR;
        end
        S_TOKEN: xfer(8'hff, S_TOKENB);
        S_TOKENB: begin
            if (rx_fe) begin idx <= 10'd0; st <= S_RDATA; end
            else if (!rx_ff || cnt[16]) st <= S_CMDERR;                      // 2**16 polls: about 40 ms
            else begin c_inc <= 1'b1; st <= S_TOKEN; end
        end
        S_RDATA: xfer(8'hff, S_RDATAB);
        S_RDATAB: begin
            b_we    <= 1'b1;
            idx     <= idx + 1'b1;
            if (idx_511) begin skipn <= 2'd2; skip_next <= S_RDONE; st <= S_SKIP; end          // CRC
            else st <= S_RDATA;
        end
        S_RDONE: begin
            sd_cs  <= 1'b1;
            cvalid <= 1'b1;
            cblk   <= blk;
            xfer(8'hff, S_AFTER);
        end
        S_AFTER: begin
            idx <= 10'd0;
            st  <= wr_r ? S_CPI : S_CPO;
        end
        // buffer -> PSRAM (read command)
        S_CPO: begin
            b_raddr <= {half, idx[7:0]};
            st      <= S_CPO1;
        end
        S_CPO1: st <= S_CPO2;
        S_CPO2: begin
            m_wr   <= 1'b1;
            st     <= S_CPO3;
        end
        S_CPO3: st <= S_CPO4;
        S_CPO4: if (!m_busy) begin
            idx <= idx + 1'b1;
            st  <= idx_255 ? S_END : S_CPO;
        end
        // PSRAM -> buffer (write command)
        S_CPI: begin
            m_rd   <= 1'b1;
            st     <= S_CPI1;
        end
        S_CPI1: st <= S_CPI2;
        S_CPI2: if (!m_busy) begin
            b_we    <= 1'b1;
            idx     <= idx + 1'b1;
            st      <= idx_255 ? S_WR1 : S_CPI;
        end
        // CMD24: write the buffer as a block
        S_WR1: begin
            cvalid <= 1'b0;
            sd_cs  <= 1'b0;
            send_cmd(6'd24, blk_arg, 8'hff, S_WR2);
        end
        S_WR2: begin
            if (r1_ok) xfer(8'hff, S_WTOKEN);
            else st <= S_CMDERR;
        end
        S_WTOKEN: xfer(8'hfe, S_WDATA0);
        S_WDATA0: begin idx <= 10'd0; st <= S_WDATA; end
        S_WDATA: begin b_raddr <= idx[8:0]; st <= S_WDATA1; end
        S_WDATA1: st <= S_WDATA2;
        S_WDATA2: xfer(b_q, S_WDATA3);
        S_WDATA3: begin
            idx <= idx + 1'b1;
            if (idx_511) begin skipn <= 2'd2; skip_next <= S_WRESP; st <= S_SKIP; end          // CRC
            else st <= S_WDATA;
        end
        S_WRESP: xfer(8'hff, S_WRESPB);
        S_WRESPB: begin
            c_clr <= 1'b1;
            st  <= rx_resp ? S_WBUSY : S_CMDERR;
        end
        S_WBUSY: xfer(8'hff, S_WBUSYB);
        S_WBUSYB: begin
            if (rx_ff) begin cvalid <= 1'b1; cblk <= blk; st <= S_END; end
            else if (cnt[19]) st <= S_CMDERR;                                 // 2**19 polls: about 0.4 s
            else begin c_inc <= 1'b1; st <= S_WBUSY; end
        end
        // ------------------------------------------------ the end of a command
        S_END: begin
            sd_cs  <= 1'b1;
            result <= 8'h00;
            xfer(8'hff, S_ENDB);
        end
        S_CMDERR: begin
            sd_cs  <= 1'b1;
            cvalid <= 1'b0;
            result <= 8'hff;
            xfer(8'hff, S_ENDB);
        end
        S_ENDB: begin
            pend <= 1'b0;
            exec <= 1'b0;
            st   <= S_IDLE;
        end
        default: st <= S_RST;
        endcase
    end

endmodule
