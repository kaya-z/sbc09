// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// After the reset an image is copied from the board's SPI flash (offset j_flash, written with openFPGALoader -o) to
// the PSRAM (j_psram), byte by byte.  Two jobs use it, one after the other (sbc09_soc.v): the kanji image for the HDMI
// screen (rtl/kanji.vh, scripts/mkkanji.py; j_magic = 1: if the flash does not start with the image's magic, nothing is
// copied and ok stays 0: the kanji show as a box) and the ROM image of the Level 2 machine (os9/os9v2.rom, no magic).
// The flash is read with the plain READ command (03h) at clk/4 = 20 MHz.

module kanji_loader (
    input  wire        clk,
    input  wire        rst,
    input  wire        start,
    input  wire [23:0] j_flash,             // the job: the flash address of the image, the PSRAM address, the size in bytes, the check of 'KJF1'
    input  wire [21:0] j_psram,
    input  wire [18:0] j_size,
    input  wire        j_magic,
    output reg         done,
    output reg         ok,
    output reg         fl_cs,
    output reg         fl_clk,
    output reg         fl_mosi,
    input  wire        fl_miso,
    output reg         wr,                  // a one clock pulse: write wr_data to the PSRAM at wr_addr
    output reg  [21:0] wr_addr,
    output reg  [7:0]  wr_data,
    input  wire        busy,                // the PSRAM request is under way (it also covers the pulse's own clock)
    output wire [15:0] dbg                  // for DEBUG builds: ok, done, the state, the first byte read from the flash
);
    // the SPI byte engine (mode 0)
    reg        sgo, sbusy, sdone;
    reg  [7:0] stx, srx, sh_in, sh_out;
    reg  [2:0] bcnt;
    reg  [1:0] hc;
    always @(posedge clk) begin
        sdone <= 1'b0;
        if (rst) begin sbusy <= 1'b0; fl_clk <= 1'b0; fl_mosi <= 1'b1; end
        else if (!sbusy) begin
            fl_clk <= 1'b0;
            if (sgo) begin sbusy <= 1'b1; fl_mosi <= stx[7]; sh_out <= {stx[6:0], 1'b1}; bcnt <= 3'd0; hc <= 2'd0; end
        end else if (hc != 2'd1) hc <= hc + 1'b1;
        else begin
            hc <= 2'd0;
            if (!fl_clk) begin fl_clk <= 1'b1; sh_in <= {sh_in[6:0], fl_miso}; end
            else begin
                fl_clk  <= 1'b0;
                fl_mosi <= sh_out[7];
                sh_out  <= {sh_out[6:0], 1'b1};
                bcnt    <= bcnt + 1'b1;
                if (bcnt == 3'd7) begin sbusy <= 1'b0; sdone <= 1'b1; srx <= sh_in; end
            end
        end
    end

    localparam [3:0] L_IDLE = 4'd0, L_CS = 4'd1, L_CMD = 4'd2, L_CMDW = 4'd3, L_RD = 4'd4, L_RDW = 4'd5,
                     L_CHK = 4'd6, L_WR = 4'd7, L_WRW = 4'd8, L_WRW2 = 4'd9, L_END = 4'd10,
                     L_PRE = 4'd11, L_PREW = 4'd12, L_PWT = 4'd13;
    reg [3:0]  st;
    reg [7:0]  b0 = 8'h00;
    reg [3:0]  tries = 4'd0;                            // failed attempts so far (the magic was wrong: the flash was not ready)
    assign dbg = {ok, done, 2'b00, tries, b0};
    reg [2:0]  ci;
    reg [17:0] n;
    reg        n_lt4, n_last;
    reg [7:0]  magic;
    reg [31:0] cmd;
    reg [3:0]  pi;                                      // step of the flash wake-up sequence
    reg [13:0] pw;                                      // its waiting time

    // registers that follow the byte counter (it changes once per byte, which takes 40 clocks)
    always @(posedge clk) begin
        n_lt4   <= (n[17:2] == 16'd0);
        n_last  <= (n == j_size - 1'b1);
        magic   <= (n[1:0] == 2'd0) ? 8'h4b : (n[1:0] == 2'd1) ? 8'h4a : (n[1:0] == 2'd2) ? 8'h46 : 8'h31;     // K J F 1
        wr_addr <= j_psram + {4'd0, n};
    end

    always @(posedge clk) begin
        sgo <= 1'b0;
        wr  <= 1'b0;
        if (rst) begin
            st <= L_IDLE; done <= 1'b0; ok <= 1'b0; fl_cs <= 1'b1;
        end else case (st)
        L_IDLE: if (start) begin done <= 1'b0; ok <= 1'b0; tries <= 4'd0; pi <= 4'd0; st <= L_PRE; end
        // The flash may be in the state the configuration left it in (continuous read mode, power down ...): leave it
        // (FFh FFh), release it from power down (ABh), reset it (66h 99h), then read.
        L_PRE: case (pi)
            4'd0:  begin fl_cs <= 1'b0; stx <= 8'hff; sgo <= 1'b1; st <= L_PREW; end
            4'd1:  begin stx <= 8'hff; sgo <= 1'b1; st <= L_PREW; end
            4'd2:  begin fl_cs <= 1'b1; pw <= 14'd4000; st <= L_PWT; end
            4'd3:  begin fl_cs <= 1'b0; stx <= 8'hab; sgo <= 1'b1; st <= L_PREW; end
            4'd4:  begin fl_cs <= 1'b1; pw <= 14'd4000; st <= L_PWT; end
            4'd5:  begin fl_cs <= 1'b0; stx <= 8'h66; sgo <= 1'b1; st <= L_PREW; end
            4'd6:  begin fl_cs <= 1'b1; pw <= 14'd200; st <= L_PWT; end
            4'd7:  begin fl_cs <= 1'b0; stx <= 8'h99; sgo <= 1'b1; st <= L_PREW; end
            4'd8:  begin fl_cs <= 1'b1; pw <= 14'd12000; st <= L_PWT; end
            default: begin
`ifdef DEBUG_FLASH0
                cmd <= {8'h03, 24'h000016};              // DEBUG=3: read the bitstream's own first bytes (offset 0x16 is 0xA5), to see if the flash can be read at all
`else
                cmd <= {8'h03, j_flash};
`endif
                fl_cs <= 1'b0; ci <= 3'd0; n <= 18'd0; st <= L_CMD;
            end
        endcase
        L_PREW: if (sdone) begin pi <= pi + 1'b1; st <= L_PRE; end
        L_PWT: if (pw == 14'd0) begin pi <= pi + 1'b1; st <= L_PRE; end else pw <= pw - 1'b1;
        L_CMD: begin                                    // 03h and the 24 bit address
            if (ci == 3'd4) st <= L_RD;
            else begin stx <= cmd[31:24]; cmd <= {cmd[23:0], 8'h00}; sgo <= 1'b1; ci <= ci + 1'b1; st <= L_CMDW; end
        end
        L_CMDW: if (sdone) st <= L_CMD;
        L_RD: begin stx <= 8'hff; sgo <= 1'b1; st <= L_RDW; end
        L_RDW: if (sdone) st <= L_CHK;
        L_CHK: begin
            if (n[17:0] == 18'd0) b0 <= srx;
            // the magic of the image: K J F 1
            if (j_magic && n_lt4 && srx != magic) begin
                if (tries == 4'd7) st <= L_END;         // give up: the kanji show as a box
                else begin tries <= tries + 1'b1; fl_cs <= 1'b1; pw <= 14'd1000; pi <= 4'd15; st <= L_PWT; end   // try again from the start
            end
            else begin wr_data <= srx; wr <= 1'b1; st <= L_WR; end
        end
        L_WR: st <= L_WRW;                              // the pulse is on its way
        L_WRW: if (!busy) st <= L_WRW2;
        L_WRW2: begin
            n <= n + 1'b1;
            if (n_last) begin ok <= 1'b1; st <= L_END; end
            else st <= L_RD;
        end
        L_END: begin fl_cs <= 1'b1; done <= 1'b1; st <= L_IDLE; end
        default: st <= L_IDLE;
        endcase
    end
endmodule
