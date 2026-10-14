// SPDX-License-Identifier: MIT
// 27 MHz crystal, and the PLL's outputs: 67.5 MHz and the same shifted by 90 degrees (the clock pin of the PSRAM)
create_clock -name sys_clk -period 37.037 -waveform {0 18.518} [get_ports {sys_clk}]
create_generated_clock -name clk81 -source [get_ports {sys_clk}] -master_clock sys_clk -multiply_by 5 -divide_by 2 [get_pins {pll/rpll_inst/CLKOUT}]
create_generated_clock -name clk81p -source [get_ports {sys_clk}] -master_clock sys_clk -multiply_by 5 -divide_by 2 -phase 90 [get_pins {pll/rpll_inst/CLKOUTP}]

// The CPU (vendor/mc6809) is clocked by E and Q, which sbc09_soc.v makes from the 67.5 MHz clock:
// one bus cycle = 4 phases of PH_DIV = 9 clocks = 533.3 ns.  E falls at 0 and rises after 2 phases;
// Q rises after 1 phase and falls after 3 (the CPU acts on the falling edges).
create_clock -name E -period 533.33 -waveform {266.67 533.33} [get_nets {soc/e}]
create_clock -name Q -period 533.33 -waveform {133.33 400.0} [get_nets {soc/q}]

// The 67.5 MHz logic and the CPU exchange data only at times when the other side is quiet: the address, data
// and control of a cycle are used a phase or more after E has fallen, and the data to the CPU is
// stable (and the CPU's clock held until it is) before E falls.  So these paths are not timed.
set_false_path -from [get_clocks {clk81}] -to [get_clocks {E Q}]
set_false_path -from [get_clocks {E Q}] -to [get_clocks {clk81}]
