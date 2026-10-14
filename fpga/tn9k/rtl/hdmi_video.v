// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The display side of the HDMI text screen: 640x480 at 60 Hz (pixel clock 25.2 MHz, negative sync), with an
// 80 x ROWS screen of 8x16 characters in the middle (a black border above and below).  Per cell it reads
// the video RAM (character, colours) and the font ROM; the colours are the 16 ANSI colours; the cursor is a
// block that blinks.
//
// Timing of the fetch (in pixel clocks, p0..p7 = the clock within the 8 pixel cell that is being shown): during
// cell c the data of cell c+1 is fetched (the first cell of a line, during the last cell of the line before),
//   p0 row*80  p1 video RAM address  p2 (RAM reads)  p3 font address  p4 (ROM reads)  p5 glyph  p7 -> the shifter

module hdmi_video #(
    parameter ROWS = 25
) (
    input  wire        clk,                 // pixel clock
    input  wire        rst,
    output reg  [10:0] va,
    input  wire [23:0] vq,                  // video RAM word: {colours, 16 bit code}
    output reg  [11:0] fa,
    input  wire [7:0]  fq,
    input  wire [6:0]  cur_col,             // from another clock domain: sampled twice here
    input  wire [4:0]  cur_row,
    output reg  [23:0] rgb,
    output reg         de,
    output reg         hs,
    output reg         vs,
    // the glyph lines of wide characters
    input  wire [7:0]  lbq,                 // line buffer, one clock after lb_raddr
    output reg  [7:0]  lb_raddr,
    // for the glyph engine (system clock domain): the next line
    output reg         ls_tog,
    output reg  [10:0] nl_base,
    output reg  [3:0]  nl_fl,
    output reg         nl_valid
);

    localparam H_ACTIVE = 640, H_FP = 16, H_SYNC = 96, H_TOTAL = 800;
    localparam V_ACTIVE = 480, V_FP = 10, V_SYNC = 2,  V_TOTAL = 525;
    localparam Y0 = (V_ACTIVE - 16 * ROWS) / 2;             // first line of the text

    reg [9:0] h, v;
    always @(posedge clk) begin
        if (rst) begin h <= 0; v <= 0; end
        else if (h == H_TOTAL - 1) begin
            h <= 0;
            v <= (v == V_TOTAL - 1) ? 10'd0 : v + 1'b1;
        end else h <= h + 1'b1;
    end

    wire [2:0] p    = h[2:0];
    wire [6:0] c    = h[9:3];                               // the cell being shown (0..99)
    wire       last = (c == 7'd99);                         // the last cell of the line: fetch for the next line
    wire [9:0] vn   = (v == V_TOTAL - 1) ? 10'd0 : v + 1'b1;

    // the text line of the line to fetch for
    wire [9:0] fv    = last ? vn : v;
    wire [9:0] fy    = fv - Y0;
    wire       fin_v = (fv >= Y0) && (fv < Y0 + 16 * ROWS);
    wire [6:0] fcol  = last ? 7'd0 : c + 1'b1;
    wire       fin_h = (fcol < 7'd80);

    reg [10:0] rb;
    reg        fok;
    reg [3:0]  fl;
    reg [6:0]  fc;
    reg [7:0]  ng, na;
    reg [7:0]  glyph, gattr;

    always @(posedge clk) begin
        case (p)
        3'd0: begin
            rb  <= ({6'd0, fy[8:4]} << 6) + ({6'd0, fy[8:4]} << 4);
            fl  <= fy[3:0];
            fc  <= fcol;
            fok <= fin_v && fin_h;
        end
        3'd1: va <= rb + {4'd0, fc};
        3'd3: begin
            fa <= {vq[7:0], fl};
            lb_raddr <= {last ? ~ls_tog : ls_tog, fc};         // the half of the line buffer of the line that is fetched for
        end
        3'd5: begin
            ng <= fok ? ((vq[15:12] != 4'd0) ? lbq : fq) : 8'h00;
            na <= fok ? vq[23:16] : 8'h00;
        end
        3'd7: begin
            glyph <= ng;
            gattr <= na;
        end
        default: ;
        endcase
    end

    // the next scan line, for the glyph engine: its text row and line are ready at h = 1, the toggle follows at h = 2
    wire [9:0] ny    = vn - Y0;
    always @(posedge clk) begin
        if (rst) ls_tog <= 1'b0;
        else begin
            if (h == 10'd1) begin
                nl_base  <= ({6'd0, ny[8:4]} << 6) + ({6'd0, ny[8:4]} << 4);
                nl_fl    <= ny[3:0];
                nl_valid <= (vn >= Y0) && (vn < Y0 + 16 * ROWS);
            end
            if (h == 10'd2) ls_tog <= ~ls_tog;
        end
    end

    // ---------------------------------------------------------------- the picture
    function [23:0] palette;
        input [3:0] k;
        reg [7:0] on, off;
        begin
            on  = k[3] ? 8'hff : 8'haa;
            off = k[3] ? 8'h55 : 8'h00;
            palette = {k[0] ? on : off, k[1] ? on : off, k[2] ? on : off};      // R G B
        end
    endfunction

    reg [1:0] cc1, cc2;                                     // the cursor, sampled twice
    reg [11:0] curp1, curp2;
    reg [4:0]  frames;
    wire       blink = frames[4];
    wire [9:0] yv    = v - Y0;
    wire       in_v  = (v >= Y0) && (v < Y0 + 16 * ROWS);
    wire       text  = in_v && (c < 7'd80);
    wire       here  = text && (c == curp2[6:0]) && (yv[8:4] == curp2[11:7]);
    wire       pix   = glyph[~p];
    wire       inv   = here && blink;
    wire [3:0] fgc   = gattr[3:0];
    wire [3:0] bgc   = gattr[7:4];
    wire       on    = pix ^ inv;

    always @(posedge clk) begin
        curp1 <= {cur_row, cur_col};
        curp2 <= curp1;
        if (rst) frames <= 0;
        else if (h == 0 && v == 0) frames <= frames + 1'b1;

        // one register stage for everything, so that the picture and its de / sync stay together
        rgb <= text ? (on ? palette(fgc) : palette(bgc)) : 24'd0;
        de  <= (h < H_ACTIVE) && (v < V_ACTIVE);
        hs  <= ~((h >= H_ACTIVE + H_FP) && (h < H_ACTIVE + H_FP + H_SYNC));
        vs  <= ~((v >= V_ACTIVE + V_FP) && (v < V_ACTIVE + V_FP + V_SYNC));
    end

endmodule
