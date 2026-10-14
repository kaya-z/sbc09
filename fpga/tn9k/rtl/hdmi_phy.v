// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The HDMI (DVI) pins of the Tang Nano 9K: the 126 MHz PLL, the pixel clock (126 / 5 = 25.2 MHz), the
// 10:1 serializers (OSER10, the first bit of a symbol is D0) and the differential outputs (ELVDS_OBUF)
// for the three data channels and the clock.  The PLL settings are those of Sipeed's HDMI example for this
// board (27 MHz * 14 / 3 = 126 MHz, VCO 504 MHz).  Synthesis only: the simulation uses hdmi_text directly.

module hdmi_phy (
    input  wire       clkin,                // 27 MHz
    output wire       clk_pix,              // 25.2 MHz
    output wire       locked,
    input  wire       rst_pix,
    input  wire [9:0] tmds_b,
    input  wire [9:0] tmds_g,
    input  wire [9:0] tmds_r,
    output wire       tmds_clk_p, tmds_clk_n,
    output wire [2:0] tmds_d_p, tmds_d_n
);
    wire clk_5x;
    wire gnd = 1'b0;

    rPLL #(
        .FCLKIN("27"),
        .DYN_IDIV_SEL("false"), .IDIV_SEL(2),
        .DYN_FBDIV_SEL("false"), .FBDIV_SEL(13),
        .DYN_ODIV_SEL("false"), .ODIV_SEL(4),
        .PSDA_SEL("0000"), .DYN_DA_EN("false"), .DUTYDA_SEL("1000"),
        .CLKOUT_FT_DIR(1'b1), .CLKOUTP_FT_DIR(1'b1), .CLKOUT_DLY_STEP(0), .CLKOUTP_DLY_STEP(0),
        .CLKFB_SEL("internal"), .CLKOUT_BYPASS("false"), .CLKOUTP_BYPASS("false"), .CLKOUTD_BYPASS("false"),
        .DYN_SDIV_SEL(2), .CLKOUTD_SRC("CLKOUT"), .CLKOUTD3_SRC("CLKOUT")
    ) pll (
        .CLKOUT(clk_5x), .LOCK(locked), .CLKOUTP(), .CLKOUTD(), .CLKOUTD3(),
        .RESET(gnd), .RESET_P(gnd), .CLKIN(clkin), .CLKFB(gnd),
        .FBDSEL({6{gnd}}), .IDSEL({6{gnd}}), .ODSEL({6{gnd}}),
        .PSDA({4{gnd}}), .DUTYDA({4{gnd}}), .FDLY({4{gnd}})
    );

    CLKDIV #(.DIV_MODE("5"), .GSREN("false")) hdiv (
        .CLKOUT(clk_pix), .HCLKIN(clk_5x), .RESETN(locked), .CALIB(gnd)
    );

    wire [2:0] ser;
    OSER10 ser_b (.Q(ser[0]), .D0(tmds_b[0]), .D1(tmds_b[1]), .D2(tmds_b[2]), .D3(tmds_b[3]), .D4(tmds_b[4]),
                  .D5(tmds_b[5]), .D6(tmds_b[6]), .D7(tmds_b[7]), .D8(tmds_b[8]), .D9(tmds_b[9]),
                  .PCLK(clk_pix), .FCLK(clk_5x), .RESET(rst_pix));
    OSER10 ser_g (.Q(ser[1]), .D0(tmds_g[0]), .D1(tmds_g[1]), .D2(tmds_g[2]), .D3(tmds_g[3]), .D4(tmds_g[4]),
                  .D5(tmds_g[5]), .D6(tmds_g[6]), .D7(tmds_g[7]), .D8(tmds_g[8]), .D9(tmds_g[9]),
                  .PCLK(clk_pix), .FCLK(clk_5x), .RESET(rst_pix));
    OSER10 ser_r (.Q(ser[2]), .D0(tmds_r[0]), .D1(tmds_r[1]), .D2(tmds_r[2]), .D3(tmds_r[3]), .D4(tmds_r[4]),
                  .D5(tmds_r[5]), .D6(tmds_r[6]), .D7(tmds_r[7]), .D8(tmds_r[8]), .D9(tmds_r[9]),
                  .PCLK(clk_pix), .FCLK(clk_5x), .RESET(rst_pix));

    ELVDS_OBUF obuf_d0 (.I(ser[0]), .O(tmds_d_p[0]), .OB(tmds_d_n[0]));
    ELVDS_OBUF obuf_d1 (.I(ser[1]), .O(tmds_d_p[1]), .OB(tmds_d_n[1]));
    ELVDS_OBUF obuf_d2 (.I(ser[2]), .O(tmds_d_p[2]), .OB(tmds_d_n[2]));
    ELVDS_OBUF obuf_ck (.I(clk_pix), .O(tmds_clk_p), .OB(tmds_clk_n));
endmodule
