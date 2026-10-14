// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// 27 MHz -> 67.5 MHz (27 * 5 / 2), and the same clock shifted by 90 degrees (the clock pin of the PSRAM).
// VCO 540 MHz.  The controller of vendor/psram was written for 81 MHz, but on the board used here the PSRAM test
// of its author read wrong data at 81 MHz and ran without an error at 54, 67.5 and 74.25 MHz (the result is in docs/FPGA.md).
// 67.5 MHz gives some room on both sides.

module tn9k_pll (
    input  wire clkin,
    output wire clkout,
    output wire clkoutp,
    output wire lock
);
    wire gnd = 1'b0;
    wire vcc = 1'b1;
    wire clkoutd, clkoutd3;

    rPLL #(
        .FCLKIN("27"),
        .DYN_IDIV_SEL("false"), .IDIV_SEL(1),
        .DYN_FBDIV_SEL("false"), .FBDIV_SEL(4),
        .DYN_ODIV_SEL("false"), .ODIV_SEL(8),
        .PSDA_SEL("0100"), .DYN_DA_EN("false"), .DUTYDA_SEL("1000"),
        .CLKOUT_FT_DIR(1'b1), .CLKOUTP_FT_DIR(1'b1), .CLKOUT_DLY_STEP(0), .CLKOUTP_DLY_STEP(0),
        .CLKFB_SEL("internal")
    ) rpll_inst (
        .CLKOUT(clkout), .LOCK(lock), .CLKOUTP(clkoutp), .CLKOUTD(clkoutd), .CLKOUTD3(clkoutd3),
        .RESET(gnd), .RESET_P(gnd), .CLKIN(clkin), .CLKFB(gnd),
        .FBDSEL({6{gnd}}), .IDSEL({6{gnd}}), .ODSEL({6{gnd}}),
        .PSDA({4{gnd}}), .DUTYDA({4{gnd}}), .FDLY({4{vcc}})
    );
endmodule
