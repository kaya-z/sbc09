// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// One channel of the TMDS (DVI) encoder: 8 bits of picture data, or two control bits, to a 10 bit symbol.
// Transmit bit 0 first.  After the DVI 1.0 specification, "TMDS video data coding".

module tmds_enc (
    input  wire       clk,
    input  wire       rst,
    input  wire       de,                   // 1: picture data, 0: the control symbols
    input  wire [1:0] c,                    // control bits {c1, c0}  (blue channel: {vsync, hsync})
    input  wire [7:0] d,
    output reg  [9:0] q
);
    function [3:0] ones;
        input [7:0] x;
        integer i;
        begin
            ones = 0;
            for (i = 0; i < 8; i = i + 1) ones = ones + x[i];
        end
    endfunction

    wire [3:0] n1d = ones(d);
    wire       use_xnor = (n1d > 4) || (n1d == 4 && !d[0]);

    // stage 1: the 9 bit word with the fewer transitions
    reg [8:0] qm;
    integer i;
    always @(*) begin
        qm[0] = d[0];
        for (i = 1; i < 8; i = i + 1)
            qm[i] = use_xnor ? ~(qm[i-1] ^ d[i]) : (qm[i-1] ^ d[i]);
        qm[8] = ~use_xnor;
    end

    // stage 2: keep the running disparity near 0
    wire [3:0]  n1 = ones(qm[7:0]);
    wire [3:0]  n0 = 4'd8 - n1;
    reg signed [4:0] cnt;                   // disparity so far
    wire signed [4:0] dif = $signed({1'b0, n1}) - $signed({1'b0, n0});      // n1 - n0

    always @(posedge clk) begin
        if (rst) begin
            cnt <= 0;
            q   <= 10'b1101010100;
        end else if (!de) begin
            cnt <= 0;
            case (c)
                2'b00: q <= 10'b1101010100;
                2'b01: q <= 10'b0010101011;
                2'b10: q <= 10'b0101010100;
                2'b11: q <= 10'b1010101011;
            endcase
        end else if (cnt == 0 || n1 == n0) begin
            q[9]   <= ~qm[8];
            q[8]   <= qm[8];
            q[7:0] <= qm[8] ? qm[7:0] : ~qm[7:0];
            cnt    <= qm[8] ? cnt + dif : cnt - dif;
        end else if ((cnt > 0 && n1 > n0) || (cnt < 0 && n0 > n1)) begin
            q[9]   <= 1'b1;
            q[8]   <= qm[8];
            q[7:0] <= ~qm[7:0];
            cnt    <= cnt + (qm[8] ? 5'sd2 : 5'sd0) - dif;
        end else begin
            q[9]   <= 1'b0;
            q[8]   <= qm[8];
            q[7:0] <= qm[7:0];
            cnt    <= cnt - (qm[8] ? 5'sd0 : 5'sd2) + dif;
        end
    end
endmodule
