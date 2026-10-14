// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// A stand-in for the PSRAM controller (vendor/psram/psram_controller.v) in the simulation: the same
// user interface (read / write pulse, busy, dout) and about its timing (a read takes 12 clocks, a write 7,
// now and then a refresh makes it longer), without the HyperRAM pins.

module PsramModel #(
    parameter INIT_FILE = "",                // 16 bit words (little endian), loaded at word INIT_BASE: the kanji image of the unit test
    parameter INIT_BASE = 0,
    parameter INIT_CLKS = 400,
    parameter RLAT = 12,
    parameter WLAT = 7
) (
    input  wire        clk,
    input  wire        resetn,
    input  wire        read,
    input  wire        write,
    input  wire [21:0] addr,
    input  wire [15:0] din,
    input  wire        byte_write,
    output reg  [15:0] dout,
    output wire        busy
);
    reg [15:0] mem [0:1048575];             // 2 MB: the machine uses the first 64 KB, the kanji image is at 1 MB
    reg [15:0] cnt;
    reg        is_wr;
    reg [21:0] a;
    reg [15:0] d;
    reg        bw;
    reg [11:0] refresh;

    assign busy = (cnt != 0);
    initial if (INIT_FILE != "") $readmemh(INIT_FILE, mem, INIT_BASE);

    always @(posedge clk) begin
        if (!resetn) begin
            cnt <= INIT_CLKS;
            refresh <= 0;
            is_wr <= 1'b0;
        end else if (cnt != 0) begin
            cnt <= cnt - 1'b1;
            if (cnt == 1) begin
                if (is_wr) begin
                    if (!bw) mem[a[20:1]] <= d;
                    else if (a[0]) mem[a[20:1]][15:8] <= d[15:8];
                    else           mem[a[20:1]][7:0]  <= d[7:0];
                end else dout <= mem[a[20:1]];
            end
        end else if (read || write) begin
            is_wr <= write;
            a <= addr; d <= din; bw <= byte_write;
            refresh <= refresh + 1'b1;
            // every 2000th access takes 3 clocks more (the refresh of the DRAM)
            cnt <= (write ? WLAT : RLAT) + ((refresh % 2000) == 1999 ? 3 : 0);
        end
    end
endmodule
