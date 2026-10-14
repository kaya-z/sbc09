// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The HDMI text screen.  The console bytes of the guest go through a character decoder (hdmi_decode: UTF-8, EUC-JP,
// Shift_JIS or raw, system clock domain, reads tables from the PSRAM) to symbols for the terminal (hdmi_term),
// which keeps the screen in a video RAM; the display (hdmi_video) reads the video RAM and the character ROM, and
// the lines of the 16x16 kanji glyphs from a line buffer that hdmi_glyph fills from the PSRAM; three TMDS encoders
// make the symbols that hdmi_phy sends.  The terminal and the display run on the pixel clock (the terminal is slow
// logic that would not meet the 67.5 MHz of the system); the symbols cross over with a toggle, and the terminal's
// "queue nearly full" crosses back (the CPU waits for it, so no character is lost).  The video RAM has three
// copies: for the display, for the terminal's scroll operations, and for the glyph engine.

module hdmi_text #(
    parameter ROWS = 25,
    parameter CHARSET = 1,
    parameter FONT_FILE = "font8x16.hex"
) (
    input  wire        clk,                 // system clock (the console bytes come in here)
    input  wire        rst,
    input  wire        stb,                 // a byte written to the console
    input  wire [7:0]  din,
    output wire        busy,                // the screen cannot take more characters now: hold them back
    input  wire        kanji_ok,            // the kanji image is in the PSRAM
    // PSRAM word reads (arbiter masters 1 and 2)
    output wire        x1_req,
    output wire [21:0] x1_addr,
    input  wire        x1_ack,
    input  wire [15:0] x1_rdata,
    output wire        x2_req,
    output wire [21:0] x2_addr,
    input  wire        x2_ack,
    input  wire [15:0] x2_rdata,
    input  wire        clk_pix,             // pixel clock 25.2 MHz (the terminal runs on it, too)
    input  wire        rst_pix,
    output wire [9:0]  tmds_b,
    output wire [9:0]  tmds_g,
    output wire [9:0]  tmds_r
);

    // ---------------------------------------------------------------- the console bytes, across the clock domains
    reg        tog = 1'b0;
    reg [7:0]  dreg;
    always @(posedge clk) if (stb) begin tog <= ~tog; dreg <= din; end       // one byte per 87 us at most
    reg [2:0]  ts;
    reg        stb_p;
    reg [7:0]  din_p;
    always @(posedge clk_pix) begin
        ts    <= {ts[1:0], tog};
        stb_p <= (ts[2] != ts[1]);
        din_p <= dreg;
    end

    // ---------------------------------------------------------------- the decoder (pixel clock), PSRAM look-ups through psram_xfer
    wire        dec_busy, sym_stb, sym_wide;
    wire [11:0] sym;
    wire        xp_req, xp_ack;
    wire [21:0] xp_addr;
    wire [15:0] xp_rdata;
    hdmi_decode #(.CHARSET(CHARSET)) decode (
        .clk(clk_pix), .rst(rst_pix), .kanji_ok(kanji_ok_p), .stb(stb_p), .din(din_p), .busy(dec_busy),
        .sym_stb(sym_stb), .sym_wide(sym_wide), .sym(sym),
        .x_req(xp_req), .x_addr(xp_addr), .x_ack(xp_ack), .x_rdata(xp_rdata)
    );
    psram_xfer xfer (
        .clk_a(clk_pix), .req_a(xp_req), .addr_a(xp_addr), .ack_a(xp_ack), .rdata_a(xp_rdata),
        .clk_b(clk), .req_b(x1_req), .addr_b(x1_addr), .ack_b(x1_ack), .rdata_b(x1_rdata)
    );
    reg [1:0] kq;
    always @(posedge clk_pix) kq <= {kq[0], kanji_ok};
    wire kanji_ok_p = kq[1];

    // the screen cannot take more characters: the decoder or the terminal queue is busy (back to the system clock domain)
    wire busy_p;
    reg [1:0] bs;
    always @(posedge clk) bs <= {bs[0], busy_p | dec_busy};
    reg [4:0] hold = 5'd0;                      // a byte has just come in: busy, until the pixel clock domain has taken it and says so itself
    always @(posedge clk) hold <= stb ? 5'd31 : (hold != 5'd0) ? hold - 1'b1 : 5'd0;
    assign busy = bs[1] | (hold != 5'd0);

    // ---------------------------------------------------------------- video RAM (three copies) and line buffer
    wire        vw_en;
    wire [10:0] vw_addr, vr_addr, va, vc_addr;
    wire [23:0] vw_data;
    reg  [23:0] vr_data, vq, vc_data;
    reg  [23:0] vram_a [0:2047];
    reg  [23:0] vram_b [0:2047];
    reg  [23:0] vram_c [0:2047];
    always @(posedge clk_pix) begin
        if (vw_en) begin vram_a[vw_addr] <= vw_data; vram_b[vw_addr] <= vw_data; vram_c[vw_addr] <= vw_data; end
        vr_data <= vram_b[vr_addr];
        vq      <= vram_a[va];
    end
    always @(posedge clk) vc_data <= vram_c[vc_addr];

    wire        lb_we;
    wire [7:0]  lb_addr, lb_data, lb_raddr;
    reg  [7:0]  lbram [0:255];
    reg  [7:0]  lbq;
    always @(posedge clk)     if (lb_we) lbram[lb_addr] <= lb_data;
    always @(posedge clk_pix) lbq <= lbram[lb_raddr];

    // ---------------------------------------------------------------- font ROM
    reg [7:0]  font [0:4095];
    reg [7:0]  fq;
    wire [11:0] fa;
    initial $readmemh(FONT_FILE, font);
    always @(posedge clk_pix) fq <= font[fa];

    // ---------------------------------------------------------------- terminal, display, glyph engine, TMDS
    wire [6:0] cur_col;
    wire [4:0] cur_row;
    hdmi_term #(.ROWS(ROWS)) term (
        .clk(clk_pix), .rst(rst_pix), .stb(sym_stb), .din(sym), .wide(sym_wide),
        .vw_en(vw_en), .vw_addr(vw_addr), .vw_data(vw_data), .vr_addr(vr_addr), .vr_data(vr_data),
        .cur_col(cur_col), .cur_row(cur_row), .busy(busy_p)
    );

    wire [23:0] rgb;
    wire        de, hs, vs, ls_tog, nl_valid;
    wire [10:0] nl_base;
    wire [3:0]  nl_fl;
    hdmi_video #(.ROWS(ROWS)) video (
        .clk(clk_pix), .rst(rst_pix), .va(va), .vq(vq), .fa(fa), .fq(fq),
        .cur_col(cur_col), .cur_row(cur_row), .rgb(rgb), .de(de), .hs(hs), .vs(vs),
        .lbq(lbq), .lb_raddr(lb_raddr), .ls_tog(ls_tog), .nl_base(nl_base), .nl_fl(nl_fl), .nl_valid(nl_valid)
    );

    hdmi_glyph glyph (
        .clk(clk), .rst(rst), .ls_tog(ls_tog), .nl_base(nl_base), .nl_fl(nl_fl), .nl_valid(nl_valid),
        .vc_addr(vc_addr), .vc_data(vc_data), .lb_we(lb_we), .lb_addr(lb_addr), .lb_data(lb_data),
        .x_req(x2_req), .x_addr(x2_addr), .x_ack(x2_ack), .x_rdata(x2_rdata)
    );

    tmds_enc enc_b (.clk(clk_pix), .rst(rst_pix), .de(de), .c({vs, hs}), .d(rgb[7:0]),   .q(tmds_b));
    tmds_enc enc_g (.clk(clk_pix), .rst(rst_pix), .de(de), .c(2'b00),    .d(rgb[15:8]),  .q(tmds_g));
    tmds_enc enc_r (.clk(clk_pix), .rst(rst_pix), .de(de), .c(2'b00),    .d(rgb[23:16]), .q(tmds_r));
endmodule
