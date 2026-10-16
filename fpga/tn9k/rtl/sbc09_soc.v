// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The sim09 / sbc09 "v09" machine (Level 1: 64 KB flat memory, I/O page at $E000) as a system on a chip:
//
//   CPU       Greg Miller's cycle accurate MC6809 (vendor/mc6809, BSD-3-Clause), E = clk / (4 * PH_DIV)
//   RAM       64 KB in the PSRAM of the Tang Nano 9K, through the controller of Feng Zhou (vendor/psram);
//             cleared (byte by byte) after every reset, like the memory of the simulator
//   ROM       a read-only image in block RAM, from ROM_BASE up to $FFFF, over the RAM
//   $E000/01  console ACIA on the UART (acia_uart.v)
//   $E00F     power control: $A5 stops the machine (until the reset button)
//   $E030-36  timer and clock (timer_rtc.v)
//   $E040-47  disk port on the microSD card (sd_disk.v)
//   $E080-1FF plain RAM
//
// The memory map and the registers are those of the simulator (see docs/FPGA.md and README.md).
//
// The machine has two modes.  It starts as the Level 1 machine above (the 256 byte boot ROM of FUZIX / MK09 reads
// the image of the OS from the card).  An image (the Level 2 loader, tools/mkl2disk.py) can switch it to the other machine of
// the simulator (sim09 -2): CoCo 3 style, 2 MB of physical memory in the PSRAM behind a MMU (two tasks of eight 8 KB
// blocks: the system map at $FFA0-A7, the user map at $FFA8-AF, the task register at $FF91), the I/O registers at $FF80
// (the same offsets as at $E000), a fixed page $FE00-$FFFF = physical $7FE00, no ROM, all of it RAM.  The switch is a write
// to the mode register (offset $0E of the I/O page): $A2 = go to the Level 2 mode (the system map is the identity for the
// blocks 0-6 and block 7 = $3F, so that the loader goes on running), $A3 = reset the CPU in the Level 2 mode (the maps as the
// simulator has them at the reset), after the loader has put the ROM image of the Level 2 machine at physical $7E000.
// The PSRAM is cleared (2 MB) after every reset of the board.

// Bus timing: one bus cycle of the CPU is four phases of PH_DIV clocks (E falls, Q rises, E rises, Q falls).
// Shortly after E has risen, the address, the direction, the data and the "valid" flag of the cycle are
// stable and the access is made.  The CPU's clock is held (before E falls) while a memory access or the
// disk is still busy, so the access time of the PSRAM or of the card never matters for the result.

`include "kanji.vh"

module sbc09_soc #(
    parameter CLK_HZ     = 67500000,
    parameter PH_DIV     = 9,                       // clks per phase; E = CLK_HZ / (4 * PH_DIV) = 1.875 MHz
    parameter BAUD_DIV   = 586,                     // 115200 baud
    parameter [15:0] ROM_BASE = 16'hC000,
    parameter ROM_AW     = 14,                      // the ROM is 2**ROM_AW bytes, up to $FFFF
    parameter ROM_FILE   = "rom.hex",
    parameter FREERUN    = 0,                       // 1: the monitor ROM v09.rom (FIRQ timer from the reset)
    parameter [23:0] DRIVE1_BLK = 24'h040000,
    parameter RTC_YEAR   = 126,
    parameter RTC_MONTH  = 1,
    parameter RTC_DAY    = 1,
    parameter RTC_HOUR   = 0,
    parameter RTC_MIN    = 0,
    parameter RTC_SEC    = 0,
    parameter MEMTEST    = 1,                       // test the PSRAM (55, AA, the address as the data) after the reset: Memory >>>> on the console
    parameter [21:0] MT_LAST = 22'h1fffff,          // the last byte to test (2 MB)
    parameter KANJI      = 1                        // copy the kanji image from the SPI flash to the PSRAM after the reset (HDMI screen)
) (
    input  wire        clk,
    input  wire        resetn,
    input  wire        rxd,
    output wire        txd,
    output wire        sd_clk,
    output wire        sd_mosi,
    input  wire        sd_miso,
    output wire        sd_cs,
    // PSRAM controller
    output wire        ps_rd,
    output wire        ps_wr,
    output wire [21:0] ps_addr,
    output wire [15:0] ps_din,
    output wire        ps_bw,
    input  wire [15:0] ps_dout,
    input  wire        ps_busy,
    // LEDs (active low), and the CPU for the test bench
    output wire [5:0]  led,
    input  wire        term_busy,           // the HDMI screen has no room for more characters now
    // the SPI flash with the kanji image, and two more PSRAM masters of the HDMI screen (word reads)
    output wire        fl_cs, fl_clk, fl_mosi,
    input  wire        fl_miso,
    input  wire        x1_req,
    input  wire [21:0] x1_addr,
    output wire        x1_ack,
    output wire [15:0] x1_rdata,
    input  wire        x2_req,
    input  wire [21:0] x2_addr,
    output wire        x2_ack,
    output wire [15:0] x2_rdata,
    output wire        kanji_ok,
    output wire        term_stb,            // a byte the guest wrote to the console (for the HDMI screen)
    output wire [7:0]  term_byte,
    output wire [111:0] dbg_regs,
    output wire        dbg_lic,
    output wire [15:0] dbg_addr,
    output wire        dbg_acc,
    output wire [15:0] dbg_status,
    output wire [15:0] dbg_kanji,
    output wire [15:0] dbg_disk,
    output wire [7:0]  dbg_din,
    output wire        dbg_rnw,
    output wire        dbg_vma
);

    localparam ECYC_PER_SEC  = CLK_HZ / (4 * PH_DIV);
    localparam ECYC_PER_TICK = ECYC_PER_SEC / 50;

    reg rst = 1'b1;
    always @(posedge clk) rst <= ~resetn;

    // ---------------------------------------------------------------- the CPU and its bus
    wire [7:0]  cpu_d_out;
    wire [15:0] cpu_a;
    wire        cpu_rnw, cpu_bs, cpu_ba, cpu_avma, cpu_lic;
    reg  [7:0]  cpu_d_in;
    reg         e, q;
    reg         nreset_cpu;
    wire        timer_irq;

    mc6809i #(.ILLEGAL_INSTRUCTIONS("GHOST")) cpu (
        .D(cpu_d_in), .DOut(cpu_d_out), .ADDR(cpu_a), .RnW(cpu_rnw),
        .E(e), .Q(q), .BS(cpu_bs), .BA(cpu_ba),
        .nIRQ(FREERUN ? 1'b1 : ~timer_irq), .nFIRQ(FREERUN ? ~timer_irq : 1'b1), .nNMI(1'b1),
        .AVMA(cpu_avma), .BUSY(), .LIC(cpu_lic),
        .nHALT(1'b1), .nRESET(nreset_cpu), .nDMABREQ(1'b1),
        .RegData(dbg_regs)
    );
    // DEBUG=5: the number of disk commands since the reset, and the low byte of the sector number of the last one
    reg [7:0] dsk_cnt = 8'd0;
    reg [7:0] dsk_lsn = 8'd0;
    always @(posedge clk) if (dgo) begin dsk_cnt <= dsk_cnt + 1'b1; dsk_lsn <= d_lsn0; end
    assign dbg_disk = {dsk_cnt, dsk_lsn};
    assign dbg_lic = cpu_lic;
    assign dbg_addr = cpu_a;
    assign dbg_acc = acc2;
    assign dbg_status = {cs_[2:0], mem_pend, acia_pend, disk_busy, pwroff, sd_st, sd_ready, sd_fail};
    assign dbg_din = cpu_d_in;
    assign dbg_rnw = cpu_rnw;
    assign dbg_vma = vma_pend;

    // ---------------------------------------------------------------- clock phases
    reg [7:0] div;
    reg [1:0] ph;
    reg       ecyc;                 // E has fallen: a bus cycle has ended
    reg       acc_go;               // E has risen: the cycle's access
    reg       acc2;                 // and one clock later (the access itself: it needs the decoded address)
    reg       mem_pend;             // a PSRAM access of the CPU is under way
    wire      disk_busy;
    wire      acia_tx_full;         // the transmitter cannot take a character now
    wire      ps_busy_m;
    wire [15:0] ps_dout_m;           // the word the arbiter read for master 0 (the CPU, the disk)
    reg       pwroff;
    reg       osrun;                // the boot ROM wrote $A4 to the power register: the OS has been started (LED4)
    reg       acia_pend;            // a character for a busy transmitter: the CPU waits
    // The byte read from the PSRAM lands in rd_reg on the clock where mem_pend falls.  The core takes its
    // inputs when E falls, so the stall is held for six more clocks (89 ns): the data has to run through the ALU
    // (the carry chain, 49 ns in the timing report) of the core before that (on the board this was a wrong result of ADDB with an operand
    // from the PSRAM, +-$10).
    reg [5:0] settle = 6'd0;
    always @(posedge clk) settle <= {settle[4:0], mem_pend | disk_busy};
    wire      stall = mem_pend | disk_busy | pwroff | acia_pend | (|settle);

    always @(posedge clk) begin
        ecyc   <= 1'b0;
        acc_go <= 1'b0;
        acc2   <= acc_go;
        if (rst) begin
            div <= 0; ph <= 0; e <= 1'b1; q <= 1'b0;
        end else if (div != PH_DIV - 1) begin
            div <= div + 1'b1;
        end else if (!(ph == 2'd0 && stall)) begin
            div <= 0;
            ph  <= ph + 1'b1;
            case (ph)
                2'd0: begin e <= 1'b0; ecyc <= 1'b1; end
                2'd1: q <= 1'b1;
                2'd2: begin e <= 1'b1; acc_go <= 1'b1; end
                2'd3: q <= 1'b0;
            endcase
        end
    end

    // ---------------------------------------------------------------- start-up: PSRAM, clear, reset of the CPU
    localparam [3:0] C_WAIT = 4'd0, C_CLR = 4'd1, C_WAIT2 = 4'd2, C_RUN = 4'd3, C_LOAD = 4'd4, C_LOADW = 4'd5,
                     M_HDR = 4'd6, M_W = 4'd7, M_WW = 4'd8, M_R = 4'd9, M_RW = 4'd10, M_RC = 4'd11, M_FIN = 4'd12;
    localparam [21:0] CLR_LAST = 22'h1fffff;                              // the last byte to clear: 2 MB (the Level 2 mode)
    reg [3:0]  cs_;
    reg [21:0] cw;
    reg        c_wr, t_rd;
    reg        ld_start;
    wire       ld_done, ld_wr;
    reg [3:0]  rcnt;
    reg        go_req = 1'b0;           // a one clock pulse: the guest wrote $A3 to the mode register
    reg        l2 = 1'b0;               // the Level 2 mode
    reg [3:0]  bsy_d;               // busy low for a while: the PSRAM is up

    // The memory test (before the clear): three passes over 0 ... MT_LAST, each one writes all, then reads all back:
    // 0 = $55, 1 = $AA, 2 = the address as the data (folded to 8 bits).  tp = 3 is the clear (zero bytes).
    reg [1:0]  tp;
    wire [7:0] tdata = (tp == 2'd0) ? 8'h55 : (tp == 2'd1) ? 8'hAA : (tp == 2'd2) ? (cw[7:0] ^ cw[15:8] ^ {2'b00, cw[21:16]}) : 8'h00;
    reg        t_fail;
    reg [21:0] t_faddr;
    reg [3:0]  mi;                                   // the index in the message
    // the console output of the test: one character at a time (the ACIA and the screen)
    reg        p_go, st_wr, st_d, st_d2;
    reg [7:0]  p_ch, st_byte;
    function [7:0] hexc(input [3:0] n); hexc = (n < 4'd10) ? (8'h30 + n) : (8'h37 + n); endfunction
    function [7:0] msg(input [3:0] i, input fail, input [21:0] fa);
        if (!fail) case (i)
            4'd0: msg = " "; 4'd1: msg = "O"; 4'd2: msg = "K"; 4'd3: msg = " "; 4'd4: msg = "2"; 4'd5: msg = "0";
            4'd6: msg = "4"; 4'd7: msg = "8"; 4'd8: msg = "K"; 4'd9: msg = 8'h0d; 4'd10: msg = 8'h0a; default: msg = 8'h00;
        endcase else case (i)
            4'd0: msg = " "; 4'd1: msg = "N"; 4'd2: msg = "G"; 4'd3: msg = "@";
            4'd4: msg = hexc({2'b00, fa[21:20]}); 4'd5: msg = hexc(fa[19:16]); 4'd6: msg = hexc(fa[15:12]);
            4'd7: msg = hexc(fa[11:8]); 4'd8: msg = hexc(fa[7:4]); 4'd9: msg = hexc(fa[3:0]);
            4'd10: msg = 8'h0d; 4'd11: msg = 8'h0a; default: msg = 8'h00;
        endcase
    endfunction
    // registered (the address and the pass are stable for several clocks before the values are used)
    reg [7:0]  td_r, t_got_r;
    reg [21:0] cw_n;                                 // cw + 1, registered (the carry chain of the counter was the critical path)
    reg        cw_last, cw_pg;
    always @(posedge clk) begin
        td_r    <= tdata;
        cw_n    <= cw + 1'b1;
        t_got_r <= cw[0] ? ps_dout_m[15:8] : ps_dout_m[7:0];
        cw_last <= (cw == MT_LAST);
        cw_pg   <= (cw[17:0] == 18'h3ffff);
    end

    always @(posedge clk) begin
        c_wr <= 1'b0;
        t_rd <= 1'b0;
        ld_start <= 1'b0;
        st_wr <= 1'b0; st_d <= st_wr; st_d2 <= st_d;
        if (p_go && !st_wr && !st_d && !st_d2 && !acia_tx_full && !term_busy) begin st_wr <= 1'b1; st_byte <= p_ch; p_go <= 1'b0; end
        if (rst) begin
            cs_ <= C_WAIT; cw <= 0; bsy_d <= 0; rcnt <= 0; nreset_cpu <= 1'b0;
            tp <= MEMTEST ? 2'd0 : 2'd3; t_fail <= 1'b0; t_faddr <= 22'd0; mi <= 4'd0; p_go <= 1'b0; p_ch <= 8'h00;
        end else begin
            bsy_d <= {bsy_d[2:0], ps_busy_m};
            if (go_req) begin nreset_cpu <= 1'b0; rcnt <= 4'd0; end      // the mode register: reset the CPU (in the Level 2 mode)
            case (cs_)
                C_WAIT:  if (bsy_d == 4'b0000 && !ps_busy) cs_ <= MEMTEST ? M_HDR : C_CLR;
                M_HDR: if (!p_go && !st_wr && !st_d) begin                    // "Memory "
                    case (mi)
                        4'd0: p_ch <= "M"; 4'd1: p_ch <= "e"; 4'd2: p_ch <= "m"; 4'd3: p_ch <= "o";
                        4'd4: p_ch <= "r"; 4'd5: p_ch <= "y"; default: p_ch <= " ";
                    endcase
                    p_go <= 1'b1;
                    if (mi == 4'd6) begin mi <= 4'd0; cs_ <= M_W; end else mi <= mi + 1'b1;
                end
                M_W: begin c_wr <= 1'b1; cs_ <= M_WW; end
                M_WW: if (!c_wr && !ps_busy_m && !bsy_d[0] && !bsy_d[1]) begin
                    if (cw_last) begin cw <= 0; cs_ <= M_R; end
                    else begin cw <= cw_n; cs_ <= M_W; end
                end
                M_R: begin t_rd <= 1'b1; cs_ <= M_RW; end
                M_RW: if (!t_rd && !ps_busy_m && !bsy_d[0] && !bsy_d[1]) cs_ <= M_RC;
                M_RC: if (!p_go) begin
                    if (t_got_r != td_r && !t_fail) begin t_fail <= 1'b1; t_faddr <= cw; end
                    if (cw_pg || cw_last) begin p_ch <= ">"; p_go <= 1'b1; end   // one > for 256 KB
                    if (cw_last) begin
                        cw <= 0; tp <= tp + 1'b1;
                        cs_ <= (tp == 2'd2) ? M_FIN : M_W;
                    end else begin cw <= cw_n; cs_ <= M_R; end
                end
                M_FIN: if (!p_go && !st_wr && !st_d) begin                    // " OK 2048K" or " NG @address"
                    if (msg(mi, t_fail, t_faddr) == 8'h00) cs_ <= C_CLR;       // then clear (tp = 3: zero bytes)
                    else begin p_ch <= msg(mi, t_fail, t_faddr); p_go <= 1'b1; mi <= mi + 1'b1; end
                end
                C_CLR: begin                            // write zero bytes 0, 1, 2 ... 65535 (2 MB - 1 for Level 2; the access the controller was tested with)
                    c_wr <= 1'b1;
                    cs_  <= C_WAIT2;
                end
                C_WAIT2: if (!c_wr && !ps_busy_m && !bsy_d[0] && !bsy_d[1]) begin
                    if (cw == CLR_LAST) cs_ <= C_LOAD;
                    else begin cw <= cw_n; cs_ <= C_CLR; end
                end
                C_LOAD:  if (KANJI) begin ld_start <= 1'b1; cs_ <= C_LOADW; end else cs_ <= C_RUN;     // the kanji image
                C_LOADW: if (ld_done) cs_ <= C_RUN;
                C_RUN: if (ecyc) begin
                    if (rcnt == 4'd15) nreset_cpu <= 1'b1;
                    else rcnt <= rcnt + 1'b1;
                end
                default: cs_ <= C_WAIT;
            endcase
        end
    end
    reg run = 1'b0;                             // the start-up is over (a register of its own: it selects the PSRAM path)
    always @(posedge clk) run <= !rst && (cs_ == C_RUN);

    wire [21:0] ld_addr;
    wire [7:0]  ld_data;
    kanji_loader loader (
        .clk(clk), .rst(rst), .start(ld_start), .done(ld_done), .ok(kanji_ok),
        .j_flash(`KJ_FLASH), .j_psram(`KJ_PSRAM), .j_size(`KJ_SIZE), .j_magic(1'b1),
        .fl_cs(fl_cs), .fl_clk(fl_clk), .fl_mosi(fl_mosi), .fl_miso(fl_miso),
        .wr(ld_wr), .wr_addr(ld_addr), .wr_data(ld_data), .busy(ps_busy_m), .dbg(dbg_kanji)
    );

    // ---------------------------------------------------------------- the access of a cycle
    reg        vma_pend;            // AVMA of the previous cycle = "valid" of this one
    reg [7:0]  rd_reg;
    reg        c_rd, c_cwr;
    reg [21:0] c_addr;
    reg [7:0]  c_wdata;
    reg        c_rsel;
    reg        c_isrd;              // the access under way is a read
    reg        mp_wait;             // one clk for the busy flag of the controller to show up

    // I/O page decode
    wire [15:0] a      = cpu_a;
    wire [21:0] pa;
    // The address is stable for two phases before it is used, so its decoding is registered (one clock late).
    // Level 1: the registers are at $E000 (the page $E000-$E1FF); Level 2: at $FF80 (the same offsets), in the fixed page.
    wire        in_io  = l2 ? (a[15:7] == 9'h1ff) : (a[15:9] == 7'b1110000);
    wire [8:0]  off    = l2 ? {2'b00, a[6:0]} : a[8:0];
    reg io_acia, io_power, io_timer, io_disk, io_hit, io_quiet, rom_sel, io_mmu, io_map, io_ta, io_modr;
    // Level 2: the registers of the clock are in the fixed page, and the OS reads the time with F$Move, i.e. through another
    // window of the same physical page ($7FFB1-B6).  In the simulator the registers live in that RAM; here a read of that
    // physical address gives the registers of the clock too.  (pa is declared below: it is the physical address of a.)
    reg  [21:0] pa_r;                                          // pa, a clock late: the decode of the alias is a clock later than the others (the address is stable long before it is used)
    always @(posedge clk) pa_r <= pa;
    wire        ta     = l2 && !in_io && (pa_r[21:3] == 19'h0fff6) && (pa_r[2:0] <= 3'd6);
    always @(posedge clk) begin
        io_ta    <= ta;
        io_modr  <= in_io && off == 9'h00e;                        // the mode register
        io_acia  <= in_io && off[8:1] == 8'd0;                   // $E000, $E001
        io_power <= in_io && off == 9'h00f;
        io_timer <= in_io && off >= 9'h030 && off <= 9'h036;
        io_disk  <= in_io && off >= 9'h040 && off <= 9'h047;
        io_mmu   <= l2 && in_io && off == 9'h011;            // the task register
        io_map   <= l2 && in_io && off >= 9'h020 && off <= 9'h02f;   // the system map, the user map
        io_hit   <= (in_io && ((off[8:1] == 8'd0) || off == 9'h00f || off == 9'h00e || (off >= 9'h030 && off <= 9'h036) || (off >= 9'h040 && off <= 9'h047)
                              || (l2 && (off == 9'h011 || (off >= 9'h020 && off <= 9'h02f))))) || ta;
        io_quiet <= !l2 && in_io && off < 9'h080 && !((off[8:1] == 8'd0) || off == 9'h00f || off == 9'h00e || (off >= 9'h030 && off <= 9'h036) || (off >= 9'h040 && off <= 9'h047));
        rom_sel  <= !l2 && (a >= ROM_BASE);
    end

    // The MMU of the Level 2 machine: eight 8 KB blocks per task, the block numbers (up to 2 MB); the fixed page $FE00-$FFFF
    // is always physical $7FE00.  After the reset the system map is the one of the simulator: blocks 0-6 = $FF, block 7 = $3F.
    reg [7:0] map_s [0:7];
    reg [7:0] map_u [0:7];
    reg       tr;                                                // the task: 0 = the system map, 1 = the user map
    reg [7:0] tr_raw;                                            // what the guest wrote to $FF91
    wire [7:0]  blk    = (a[15:9] == 7'b1111111) ? 8'h3f : (tr ? map_u[a[15:13]] : map_s[a[15:13]]);
    assign      pa     = {1'b0, blk, a[12:0]};                   // the physical address of the CPU's address
    integer     mk;

    // ROM
    reg [7:0] rom [0:(1 << ROM_AW) - 1];
    reg [7:0] rom_q;
    initial $readmemh(ROM_FILE, rom);
    always @(posedge clk) rom_q <= rom[a[ROM_AW-1:0]];

    // devices
    wire [7:0] acia_status, acia_rdata;
    wire [7:0] timer_rdata;
    reg        acia_rd;                 // a clock after the read: rd_reg has taken the character by then
    always @(posedge clk) acia_rd <= acc2 && vma_pend && cpu_rnw && io_acia && off[0];
    reg  [7:0] acia_byte;
    wire       acia_wr = acia_pend && !acia_tx_full && !term_busy;    // the screen can also hold a character back
    assign term_stb  = acia_wr | st_wr;
    assign term_byte = st_wr ? st_byte : acia_byte;
    wire       timer_wr = acc2 && vma_pend && !cpu_rnw && io_timer;
    wire       iack     = acc2 && cpu_bs && !cpu_ba;

    acia_uart #(.BAUD_DIV(BAUD_DIV)) acia (
        .clk(clk), .rst(rst), .rxd(rxd), .txd(txd),
        .rd_data(acia_rd), .wr_data(acia_wr | st_wr), .wdata(st_wr ? st_byte : acia_byte),
        .tx_full(acia_tx_full), .status(acia_status), .rdata(acia_rdata)
    );

    timer_rtc #(
        .FREERUN(FREERUN), .ECYC_PER_TICK(ECYC_PER_TICK), .ECYC_PER_SEC(ECYC_PER_SEC),
        .START_YEAR(RTC_YEAR), .START_MONTH(RTC_MONTH), .START_DAY(RTC_DAY),
        .START_HOUR(RTC_HOUR), .START_MIN(RTC_MIN), .START_SEC(RTC_SEC)
    ) timer (
        .clk(clk), .rst(rst), .ecyc(ecyc),
        .wr(timer_wr), .off(off[2:0]), .wdata(cpu_d_out), .iack(iack),
        .rdata(timer_rdata), .irq(timer_irq)
    );

    // disk registers
    reg [7:0]  d_drive, d_lsn2, d_lsn1, d_lsn0, d_bufh, d_bufl, d_47;
    reg [7:0]  e040;
    reg        dgo, dwr;
    wire [7:0] disk_result;
    wire       d_rd, d_wr;
    wire [21:0] d_addr;
    wire [7:0]  d_din;
    wire        sd_ready, sd_fail, sd_active;
    wire [5:0]  sd_st;
    reg         disk_busy_d;

    sd_disk #(.DRIVE1_BLK(DRIVE1_BLK)) disk (
        .clk(clk), .rst(rst),
        .go(dgo), .wr(dwr), .drive(d_drive), .lsn({d_lsn2, d_lsn1, d_lsn0}), .buf_addr({d_bufh, d_bufl}),
        .busy(disk_busy), .result(disk_result),
        .m_rd(d_rd), .m_wr(d_wr), .m_addr(d_addr), .m_din(d_din), .m_dout(ps_dout_m), .m_busy(ps_busy_m),
        .sd_clk(sd_clk), .sd_mosi(sd_mosi), .sd_miso(sd_miso), .sd_cs(sd_cs),
        .ready(sd_ready), .fail(sd_fail), .active(sd_active), .dbg_st(sd_st)
    );

    // io read data
    reg [7:0] io_rdata;
    always @(*) begin
        io_rdata = 8'h00;
        if (io_acia)       io_rdata = off[0] ? acia_rdata : acia_status;
        else if (io_timer || io_ta) io_rdata = timer_rdata;
        else if (io_mmu)   io_rdata = tr_raw;
        else if (io_map)   io_rdata = off[3] ? map_u[off[2:0]] : map_s[off[2:0]];
        else if (io_disk)
            case (off[2:0])
                3'd0: io_rdata = e040;
                3'd1: io_rdata = d_drive;
                3'd2: io_rdata = d_lsn2;
                3'd3: io_rdata = d_lsn1;
                3'd4: io_rdata = d_lsn0;
                3'd5: io_rdata = d_bufh;
                3'd6: io_rdata = d_bufl;
                default: io_rdata = d_47;
            endcase
    end

    // a write to the disk registers: noted at the access, done one clock later (c_wdata holds the byte)
    reg       w_disk = 1'b0;
    reg [2:0] w_off;
    always @(posedge clk) begin
        w_disk <= acc2 && vma_pend && !cpu_rnw && io_disk;
        w_off  <= off[2:0];
    end

    always @(posedge clk) begin
        dgo  <= 1'b0;
        go_req <= 1'b0;
        c_rd <= 1'b0;
        c_cwr <= 1'b0;
        disk_busy_d <= disk_busy;
        if (disk_busy_d && !disk_busy) e040 <= disk_result;
        if (rst) begin
            mem_pend <= 1'b0; mp_wait <= 1'b0; vma_pend <= 1'b1; pwroff <= 1'b0; osrun <= 1'b0; acia_pend <= 1'b0;
            for (mk = 0; mk < 8; mk = mk + 1) begin map_s[mk] <= 8'hff; map_u[mk] <= 8'hff; end
            map_s[7] <= 8'h3f; tr <= 1'b0; tr_raw <= 8'hff; l2 <= 1'b0;
            rd_reg <= 8'h00; e040 <= 8'h00; d_drive <= 0; d_lsn2 <= 0; d_lsn1 <= 0; d_lsn0 <= 0;
            d_bufh <= 0; d_bufl <= 0; d_47 <= 0;
        end else begin
            c_addr  <= l2 ? pa : {6'd0, a};
            c_wdata <= cpu_d_out;
            c_rsel  <= a[0];
            if (acc2) begin
                vma_pend <= cpu_avma;
                if (vma_pend) begin
                    if (io_hit) begin
                        if (cpu_rnw) rd_reg <= io_rdata;
                        else begin
                            if (io_power && cpu_d_out == 8'ha5) begin pwroff <= 1'b1; osrun <= 1'b0; end      // shutdown: LED5 on, LED4 off
                            if (io_power && cpu_d_out == 8'ha4) osrun <= 1'b1;
                            if (io_modr) begin
                                if (cpu_d_out == 8'ha2 && !l2) begin           // to the Level 2 mode: the identity map for the blocks 0-6
                                    l2 <= 1'b1; tr <= 1'b0; tr_raw <= 8'hff;
                                    for (mk = 0; mk < 7; mk = mk + 1) map_s[mk] <= mk[7:0];
                                    map_s[7] <= 8'h3f;
                                end
                                if (cpu_d_out == 8'ha3 && l2) begin             // reset the CPU with the maps of the reset
                                    go_req <= 1'b1; tr <= 1'b0; tr_raw <= 8'hff;
                                    for (mk = 0; mk < 8; mk = mk + 1) begin map_s[mk] <= 8'hff; map_u[mk] <= 8'hff; end
                                    map_s[7] <= 8'h3f;
                                end
                            end
                            if (io_mmu) begin tr_raw <= cpu_d_out; tr <= cpu_d_out[0]; end
                            if (io_map) begin if (off[3]) map_u[off[2:0]] <= cpu_d_out; else map_s[off[2:0]] <= cpu_d_out; end
                            if (io_acia && off[0]) begin
                                acia_pend <= 1'b1;                  // the guest does not look at TDRE: wait for it here
                                acia_byte <= cpu_d_out;
                            end
                        end
                    end else if (cpu_rnw && rom_sel) begin
                        rd_reg <= rom_q;
                    end else if (!cpu_rnw && (rom_sel || io_quiet)) begin
                        // read-only memory, or an unused register: the write is lost
                    end else begin                                    // RAM
                        c_rd    <= cpu_rnw;
                        c_cwr   <= ~cpu_rnw;
                        c_isrd  <= cpu_rnw;
                        mem_pend <= 1'b1;
                        mp_wait  <= 1'b1;
                    end
                end
            end
            if (w_disk)
                case (w_off)
                    3'd0: begin
                        if (c_wdata == 8'h81 || c_wdata == 8'h55) begin
                            dgo <= 1'b1; dwr <= (c_wdata == 8'h55);
                        end else e040 <= 8'hff;                       // no VRBF (host files) on the FPGA
                    end
                    3'd1: d_drive <= c_wdata;
                    3'd2: d_lsn2  <= c_wdata;
                    3'd3: d_lsn1  <= c_wdata;
                    3'd4: d_lsn0  <= c_wdata;
                    3'd5: d_bufh  <= c_wdata;
                    3'd6: d_bufl  <= c_wdata;
                    default: d_47 <= c_wdata;
                endcase
            if (acia_wr) acia_pend <= 1'b0;
            if (mem_pend) begin
                if (mp_wait) mp_wait <= 1'b0;                          // the request is on its way
                else if (!ps_busy_m) begin
                    mem_pend <= 1'b0;
                    if (c_isrd) rd_reg <= c_rsel ? ps_dout_m[15:8] : ps_dout_m[7:0];
                end
            end
        end
    end

    // master 0 of the PSRAM arbiter: the start-up (clear, kanji loader), the disk and the CPU.  Their pulses
    // become a request that is held until the arbiter has done it; ps_busy_m is high from the clock of the pulse
    // until then (so no master has to count clocks).
    // the buffer address of the disk port is a logical address: it goes through the map that is active (like the simulator)
    wire [15:0] dl     = d_addr[15:0];
    wire [7:0]  dblk   = (dl[15:9] == 7'b1111111) ? 8'h3f : (tr ? map_u[dl[15:13]] : map_s[dl[15:13]]);
    wire [21:0] d_pa   = {1'b0, dblk, dl[12:0]};
    wire use_clr  = !run;
    reg  disk_sel = 1'b0;                       // the disk owns the port (a clock late: the CPU is waiting at both ends)
    always @(posedge clk) disk_sel <= disk_busy;
    wire m_rd_s   = use_clr ? t_rd : disk_sel ? d_rd : c_rd;
    wire m_wr_s   = use_clr ? (c_wr | ld_wr) : disk_sel ? d_wr : c_cwr;
    wire m0_pulse = m_rd_s | m_wr_s;
    reg         m0_req = 1'b0, m0_wrr = 1'b0;
    reg  [21:0] m0_addr;
    reg  [7:0]  m0_din;
    wire        m0_ack;
    always @(posedge clk) begin
        if (m0_ack) m0_req <= 1'b0;
        if (m0_pulse) begin
            m0_req  <= 1'b1;
            m0_wrr  <= m_wr_s;
            m0_addr <= use_clr ? (ld_wr ? ld_addr : cw) : disk_sel ? (l2 ? d_pa : d_addr) : c_addr;
            m0_din  <= use_clr ? (ld_wr ? ld_data : td_r) : disk_sel ? d_din : c_wdata;
        end
    end
    assign ps_busy_m = m0_req;                  // (high from the clock after the pulse: nobody looks at the clock of the pulse itself)

    psram_arb arb (
        .clk(clk), .rst(rst),
        .c_rd(ps_rd), .c_wr(ps_wr), .c_addr(ps_addr), .c_din(ps_din), .c_bw(ps_bw), .c_dout(ps_dout), .c_busy(ps_busy),
        .m0_req(m0_req), .m0_wr(m0_wrr), .m0_addr(m0_addr), .m0_din(m0_din), .m0_ack(m0_ack), .m0_rdata(ps_dout_m),
        .m1_req(x1_req), .m1_addr(x1_addr), .m1_ack(x1_ack), .m1_rdata(x1_rdata),
        .m2_req(x2_req), .m2_addr(x2_addr), .m2_ack(x2_ack), .m2_rdata(x2_rdata)
    );

    // ---------------------------------------------------------------- feed the CPU
    always @(*) cpu_d_in = rd_reg;

    // ---------------------------------------------------------------- the LEDs (active low)
    reg [23:0] hb;
    always @(posedge clk) hb <= hb + 1'b1;
    // LED2: card ready (steady) / card failed (blinking); LED4: the OS has been started
    assign led = ~{pwroff, osrun, sd_active, sd_fail ? hb[22] : sd_ready, run, hb[23]};

endmodule
