// SPDX-License-Identifier: MIT
// The HDMI clocks: the PLL makes 126 MHz (= 27 * 14 / 3), a divider by 5 makes the pixel clock 25.2 MHz.
create_generated_clock -name clk_5x -source [get_ports {sys_clk}] -master_clock sys_clk -multiply_by 14 -divide_by 3 [get_pins {phy/pll/CLKOUT}]
create_generated_clock -name clk_pix -source [get_pins {phy/pll/CLKOUT}] -master_clock clk_5x -divide_by 5 [get_pins {phy/hdiv/CLKOUT}]

// The system logic (67.5 MHz) and the display (pixel clock) share only the video RAM, whose two ports have their own
// clocks, and the cursor position, which changes rarely and is sampled twice.
set_false_path -from [get_clocks {clk81}] -to [get_clocks {clk_pix}]
set_false_path -from [get_clocks {clk_pix}] -to [get_clocks {clk81}]

// The reset of the serializers (OSER10) comes from the pixel clock and is released once: it is not timed against
// the 126 MHz clock (the report would compare it with half a period of it).
set_false_path -from [get_clocks {clk_pix}] -to [get_clocks {clk_5x}]
