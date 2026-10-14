// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The glyph lines of the wide (kanji) characters of the next scan line (system clock domain).  The display cannot
// read the 16x16 kanji glyphs from the PSRAM itself, so during a scan line this engine looks at the 80 cells of the
// text row that the next line belongs to; for each left half of a wide character it reads the 16 dots (2 bytes)
// of the glyph line from the kanji image in the PSRAM and puts the two bytes into a line buffer, one byte per cell.
// The display then shows these bytes instead of a glyph of the 8x16 font.  The line buffer has two halves: the display reads
// one while this engine fills the other (ls_tog says which: it toggles at the start of every line).

`include "kanji.vh"

module hdmi_glyph (
    input  wire        clk,
    input  wire        rst,
    input  wire        ls_tog,              // from the pixel clock domain: toggles at the start of every scan line
    input  wire [10:0] nl_base,             // the video RAM address of the first cell of the next line's text row
    input  wire [3:0]  nl_fl,               // the line within the row
    input  wire        nl_valid,            // the next line is in the text area
    output reg  [10:0] vc_addr,             // video RAM, third copy (read port)
    input  wire [23:0] vc_data,
    output reg         lb_we,
    output reg  [7:0]  lb_addr,             // {half, column}
    output reg  [7:0]  lb_data,
    output reg         x_req,               // PSRAM word read (arbiter master 2)
    output reg  [21:0] x_addr,
    input  wire        x_ack,
    input  wire [15:0] x_rdata
);
    reg [2:0]  ts;
    reg        wbuf;
    reg [10:0] base;
    reg [3:0]  fl;
    reg [6:0]  c;
    reg [3:0]  st;
    reg [11:0] idx;
    localparam [3:0] E_IDLE = 4'd0, E_RD = 4'd1, E_RD1 = 4'd2, E_RD2 = 4'd3, E_WAIT = 4'd4, E_W2 = 4'd5, E_NEXT = 4'd6;

    always @(posedge clk) begin
        ts    <= {ts[1:0], ls_tog};
        lb_we <= 1'b0;
        if (rst) begin
            st <= E_IDLE; x_req <= 1'b0;
        end else case (st)
        E_IDLE: if (ts[2] != ts[1]) begin               // a new line has begun: prepare the next one
            if (nl_valid) begin
                base <= nl_base; fl <= nl_fl; wbuf <= ~ts[1]; c <= 7'd0; st <= E_RD;
            end
        end
        E_RD:  begin vc_addr <= base + {4'd0, c}; st <= E_RD1; end
        E_RD1: st <= E_RD2;
        E_RD2: begin
            if (vc_data[15:12] == 4'd1) begin           // the left half of a wide character
                idx    <= vc_data[11:0];
                x_addr <= `KJ_PSRAM + `KJ_GLYPH + {idx_w(vc_data[11:0]), 5'd0} + {17'd0, fl, 1'b0};
                x_req  <= 1'b1;
                st     <= E_WAIT;
            end else st <= E_NEXT;
        end
        E_WAIT: if (x_ack) begin
            x_req   <= 1'b0;
            lb_we   <= 1'b1;
            lb_addr <= {wbuf, c};
            lb_data <= x_rdata[7:0];
            st      <= E_W2;
        end
        E_W2: begin
            lb_we   <= 1'b1;
            lb_addr <= {wbuf, c + 7'd1};
            lb_data <= x_rdata[15:8];
            st      <= E_NEXT;
        end
        E_NEXT: begin
            if (c == 7'd79) st <= E_IDLE;
            else begin c <= c + 1'b1; st <= E_RD; end
        end
        default: st <= E_IDLE;
        endcase
    end

    function [16:0] idx_w;
        input [11:0] i;
        idx_w = {5'd0, i};
    endfunction
endmodule
