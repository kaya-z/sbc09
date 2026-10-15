// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The test bench's top level: the SoC with the PSRAM model, for Verilator (sim/tb.cpp).
module sim_top #(
    parameter PH_DIV = 9,
    parameter [15:0] ROM_BASE = 16'hFF00,
    parameter ROM_AW = 8,
    parameter ROM_FILE = "rom.hex",
    parameter FREERUN = 0
) (
    input  wire        clk,
    input  wire        resetn,
    input  wire        rxd,
    output wire        txd,
    output wire        sd_clk,
    output wire        sd_mosi,
    input  wire        sd_miso,
    output wire        sd_cs,
    output wire [5:0]  led,
    output wire        fl_cs, fl_clk, fl_mosi,
    input  wire        fl_miso,
    output wire        kanji_ok,
    output wire        term_stb,
    output wire [7:0]  term_byte,
    output wire [111:0] dbg_regs,
`ifdef HDMI
    input  wire        clk_pix,
    output wire [9:0]  tmds_b,
    output wire [9:0]  tmds_g,
    output wire [9:0]  tmds_r,
`endif
    output wire        dbg_lic,
    output wire [15:0] dbg_addr,
    output wire        dbg_acc,
    output wire [7:0]  dbg_din,
    output wire        dbg_rnw,
    output wire        dbg_vma
);
    wire        term_busy;
    wire        x1_req, x2_req, x1_ack, x2_ack;
    wire [21:0] x1_addr, x2_addr;
    wire [15:0] x1_rdata, x2_rdata;
    wire        ps_rd, ps_wr, ps_bw, ps_busy;
    wire [21:0] ps_addr;
    wire [15:0] ps_din, ps_dout;

    sbc09_soc #(.PH_DIV(PH_DIV), .ROM_BASE(ROM_BASE), .ROM_AW(ROM_AW), .ROM_FILE(ROM_FILE), .FREERUN(FREERUN)) soc (
        .clk(clk), .resetn(resetn), .rxd(rxd), .txd(txd),
        .sd_clk(sd_clk), .sd_mosi(sd_mosi), .sd_miso(sd_miso), .sd_cs(sd_cs),
        .ps_rd(ps_rd), .ps_wr(ps_wr), .ps_addr(ps_addr), .ps_din(ps_din), .ps_bw(ps_bw),
        .ps_dout(ps_dout), .ps_busy(ps_busy),
        .led(led), .dbg_status(), .dbg_kanji(), .dbg_disk(), .term_busy(term_busy),
        .fl_cs(fl_cs), .fl_clk(fl_clk), .fl_mosi(fl_mosi), .fl_miso(fl_miso),
        .x1_req(x1_req), .x1_addr(x1_addr), .x1_ack(x1_ack), .x1_rdata(x1_rdata),
        .x2_req(x2_req), .x2_addr(x2_addr), .x2_ack(x2_ack), .x2_rdata(x2_rdata), .kanji_ok(kanji_ok),
        .term_stb(term_stb), .term_byte(term_byte), .dbg_regs(dbg_regs), .dbg_lic(dbg_lic), .dbg_addr(dbg_addr), .dbg_acc(dbg_acc), .dbg_din(dbg_din), .dbg_rnw(dbg_rnw), .dbg_vma(dbg_vma)
    );

`ifdef HDMI
    reg [3:0] rp = 4'hf;
    always @(posedge clk_pix) rp <= {rp[2:0], ~resetn};
    hdmi_text #(.ROWS(25), .CHARSET(`HDMI_CHARSET), .FONT_FILE(`FONT_FILE)) screen (
        .clk(clk), .rst(~resetn), .stb(term_stb), .din(term_byte), .busy(term_busy), .kanji_ok(kanji_ok),
        .x1_req(x1_req), .x1_addr(x1_addr), .x1_ack(x1_ack), .x1_rdata(x1_rdata),
        .x2_req(x2_req), .x2_addr(x2_addr), .x2_ack(x2_ack), .x2_rdata(x2_rdata),
        .clk_pix(clk_pix), .rst_pix(rp[3]),
        .tmds_b(tmds_b), .tmds_g(tmds_g), .tmds_r(tmds_r)
    );
`else
    assign term_busy = 1'b0;
    assign x1_req = 1'b0, x1_addr = 22'd0, x2_req = 1'b0, x2_addr = 22'd0;
`endif

    PsramModel mem (
        .clk(clk), .resetn(resetn), .read(ps_rd), .write(ps_wr), .addr(ps_addr), .din(ps_din),
        .byte_write(ps_bw), .dout(ps_dout), .busy(ps_busy)
    );
endmodule
