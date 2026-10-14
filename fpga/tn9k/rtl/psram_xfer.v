// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// A PSRAM word read for a master in another clock domain (the HDMI decoder, on the pixel clock) through master 1 of
// the arbiter (system clock).  Side A raises req_a, holds it and the address until ack_a (a one clock pulse, with
// rdata_a valid); side B turns that into the arbiter's request.  The address is stable long before the request
// arrives, and the data long before the acknowledge does.

module psram_xfer (
    input  wire        clk_a,
    input  wire        req_a,
    input  wire [21:0] addr_a,
    output reg         ack_a,
    output wire [15:0] rdata_a,
    input  wire        clk_b,
    output reg         req_b,
    output reg  [21:0] addr_b,
    input  wire        ack_b,
    input  wire [15:0] rdata_b
);
    reg [2:0]  rs = 3'b000;
    reg        ack_tog = 1'b0;
    reg [15:0] hold;
    always @(posedge clk_b) begin
        rs <= {rs[1:0], req_a};
        if (rs[2:1] == 2'b01) begin addr_b <= addr_a; req_b <= 1'b1; end
        if (ack_b) begin req_b <= 1'b0; hold <= rdata_b; ack_tog <= ~ack_tog; end
    end
    reg [2:0] as = 3'b000;
    always @(posedge clk_a) begin
        as    <= {as[1:0], ack_tog};
        ack_a <= (as[2] != as[1]);
    end
    assign rdata_a = hold;
    initial req_b = 1'b0;
endmodule
