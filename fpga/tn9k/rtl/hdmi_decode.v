// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The character decoder of the HDMI screen (pixel clock domain; the PSRAM look-ups go through psram_xfer).  The bytes the guest writes to the console are in
// one of four character sets; this turns them into symbols for the terminal:
//
//   narrow, 8 bits   an ASCII control or graphic character, a half-width katakana ($A1-$DF) or a Latin-1 letter
//                    (a glyph of the 8x16 font)
//   wide, 12 bits    the number of a glyph of the 16x16 kanji image (rtl/kanji.vh): JIS X 0208 rows 1-8 and 16-47
//
//   mode 0  raw       a byte >= $80 is a glyph number of the 8x16 font (Shift_JIS half-width katakana as they are)
//   mode 1  UTF-8     2 and 3 byte sequences; Unicode to a glyph number by two look-ups in the kanji image (PSRAM)
//   mode 2  EUC-JP    $A1-$FE pairs (JIS X 0208), $8E + katakana, $8F + 2 bytes (JIS X 0212: shown as a box)
//   mode 3  Shift_JIS $81-$9F / $E0-$FC pairs, single $A1-$DF katakana
//
// A character that has no glyph (level 2 kanji and others) is shown as a box ("geta").  An invalid sequence gives
// a "?" and the byte that broke it is read again.  The mode starts as CHARSET and is changed by ESC % G (UTF-8),
// ESC % E (EUC-JP), ESC % S (Shift_JIS), ESC % @ (raw); these bytes are also passed on (the terminal swallows them).

`include "kanji.vh"

module hdmi_decode #(
    parameter CHARSET = 1
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        kanji_ok,            // the kanji image is in the PSRAM
    input  wire        stb,                 // a byte (the caller holds back the next one while busy)
    input  wire [7:0]  din,
    output wire        busy,
    output reg         sym_stb,             // a symbol for the terminal
    output reg         sym_wide,
    output reg  [11:0] sym,
    output reg         x_req,               // PSRAM word read (arbiter master 1)
    output reg  [21:0] x_addr,
    input  wire        x_ack,
    input  wire [15:0] x_rdata
);
    localparam [3:0] S_IN = 4'd0, S_CLS = 4'd10, S_PROC = 4'd1, S_EMIT = 4'd2, S_GAP = 4'd3, S_L1 = 4'd4, S_L1W = 4'd5, S_L2 = 4'd6, S_L2W = 4'd7,
                     S_WIDE = 4'd8, S_WIDE2 = 4'd9;
    reg [3:0]  st, after;
    reg [1:0]  mode;
    reg [1:0]  esc;                         // 1: ESC seen, 2: ESC % seen
    reg [7:0]  cur, l1;
    reg [2:0]  ms;                          // where in a multi-byte sequence
    reg [15:0] cp;
    reg        reproc;
    reg [3:0]  gap;
    reg [11:0] idx;
    reg [6:0]  ku, ten7;                    // JIS X 0208 row and column (1-based)
    reg        big;                         // a 4 byte UTF-8 sequence: no glyph
    // the class of the byte, worked out in the state before S_PROC (which would be too slow with it)
    reg f_esc, f_lt80, f_ge40, f_c2df, f_e0ef, f_f0f7, f_cont, f_a1fe, f_a1df, f_8e, f_8f, f_sjlead, f_sjtrail, f_ctl;
    reg [7:0] sj_a, sj_b;

    assign busy = (st != S_IN) || stb;

    // glyph number of (ku, ten) in the compact order of the image
    function [11:0] compact;
        input [5:0] k;
        input [6:0] t;
        reg [5:0] r;
        reg [11:0] b;
        begin
            r = (k <= 6'd8) ? k - 6'd1 : k - 6'd8;
            b = ({6'd0, r} << 6) + ({6'd0, r} << 5) - ({6'd0, r} << 1);       // r * 94
            compact = b + {5'd0, t} - 12'd1;
        end
    endfunction

    task emit_narrow(input [7:0] c);
        begin sym_wide <= 1'b0; sym <= {4'd0, c}; sym_stb <= 1'b1; after <= S_IN; st <= S_EMIT; end
    endtask
    task emit_wide(input [11:0] g);
        begin sym_wide <= 1'b1; sym <= g; sym_stb <= 1'b1; after <= S_IN; st <= S_EMIT; end
    endtask
    // an invalid sequence: "?" now, the byte that broke it again afterwards
    task bad;
        begin sym_wide <= 1'b0; sym <= 12'h03f; sym_stb <= 1'b1; ms <= 3'd0; reproc <= 1'b1; st <= S_EMIT; after <= S_PROC; end
    endtask

    always @(posedge clk) begin
        sym_stb <= 1'b0;
        if (rst) begin
            st <= S_IN; mode <= CHARSET; esc <= 2'd0; ms <= 3'd0; x_req <= 1'b0; reproc <= 1'b0; gap <= 4'd0;
        end else case (st)
        S_IN: if (stb) begin cur <= din; reproc <= 1'b0; st <= S_CLS; end
        S_CLS: begin
            f_esc    <= (cur == 8'h1b);
            f_lt80   <= (cur < 8'h80);
            f_ge40   <= (cur >= 8'h40);
            f_c2df   <= (cur >= 8'hc2 && cur <= 8'hdf);
            f_e0ef   <= (cur >= 8'he0 && cur <= 8'hef);
            f_f0f7   <= (cur >= 8'hf0 && cur <= 8'hf7);
            f_cont   <= (cur[7:6] == 2'b10);
            f_a1fe   <= (cur >= 8'ha1 && cur <= 8'hfe);
            f_a1df   <= (cur >= 8'ha1 && cur <= 8'hdf);
            f_8e     <= (cur == 8'h8e);
            f_8f     <= (cur == 8'h8f);
            f_sjlead <= ((cur >= 8'h81 && cur <= 8'h9f) || (cur >= 8'he0 && cur <= 8'hfc));
            f_sjtrail <= ((cur >= 8'h40 && cur <= 8'h7e) || (cur >= 8'h80 && cur <= 8'hfc));
            sj_a     <= (l1 >= 8'he0) ? l1 - 8'hc1 : l1 - 8'h81;
            sj_b     <= (cur >= 8'h80) ? cur - 8'h41 : cur - 8'h40;
            st <= S_PROC;
        end
        S_EMIT: begin gap <= 4'd2; st <= S_GAP; end                // a little time between the symbols
        S_GAP: if (gap == 4'd0) st <= after; else gap <= gap - 1'b1;
        S_PROC: begin
            if (f_esc) begin
                if (ms != 3'd0) bad;                               // ESC breaks a multi-byte character
                else begin esc <= 2'd1; emit_narrow(cur); end
            end else if (esc != 2'd0 && ms == 3'd0 && f_lt80) begin
                // ESC % X selects the character set; anything else ends the escape
                if (esc == 2'd1) esc <= (cur == 8'h25) ? 2'd2 : 2'd0;
                else begin
                    esc <= 2'd0;
                    case (cur)
                        8'h47: mode <= 2'd1;                       // G  UTF-8
                        8'h45: mode <= 2'd2;                       // E  EUC-JP
                        8'h53: mode <= 2'd3;                       // S  Shift_JIS
                        8'h40: mode <= 2'd0;                       // @  raw
                        default: ;
                    endcase
                end
                emit_narrow(cur);
            end else if (f_lt80 && !(mode == 2'd3 && ms == 3'd1 && f_ge40)) begin      // (a Shift_JIS trail byte can be < $80)
                if (ms != 3'd0) bad; else emit_narrow(cur);
            end else case (mode)
            // ------------------------------------------------ raw
            2'd0: emit_narrow(cur);
            // ------------------------------------------------ UTF-8
            2'd1: begin
                esc <= 2'd0;
                if (ms == 3'd0) begin
                    if (f_c2df) begin ms <= 3'd1; big <= 1'b0; cp <= {11'd0, cur[4:0]}; st <= S_IN; end
                    else if (f_e0ef) begin ms <= 3'd2; big <= 1'b0; cp <= {12'd0, cur[3:0]}; st <= S_IN; end
                    else if (f_f0f7) begin ms <= 3'd3; big <= 1'b1; cp <= 16'd0; st <= S_IN; end       // 4 bytes: a box
                    else begin sym_wide <= 1'b0; sym <= 12'h03f; sym_stb <= 1'b1; after <= S_IN; st <= S_EMIT; end
                end else if (f_cont) begin
                    cp <= {cp[9:0], cur[5:0]};
                    ms <= ms - 1'b1;
                    if (ms == 3'd1) st <= S_WIDE;              // complete: look at the code point
                    else st <= S_IN;
                end else bad;
            end
            // ------------------------------------------------ EUC-JP
            2'd2: begin
                esc <= 2'd0;
                case (ms)
                3'd0: begin
                    if (f_a1fe) begin l1 <= cur; ms <= 3'd1; st <= S_IN; end
                    else if (f_8e) begin ms <= 3'd2; st <= S_IN; end
                    else if (f_8f) begin ms <= 3'd3; st <= S_IN; end
                    else emit_narrow(cur);
                end
                3'd1: if (f_a1fe) begin ku <= l1[6:0] - 7'd32; ten7 <= cur[6:0] - 7'd32; ms <= 3'd0; st <= S_WIDE2; end   // l1 - $A0, cur - $A0 (mod 128)
                      else bad;
                3'd2: if (f_a1df) begin ms <= 3'd0; emit_narrow(cur); end else bad;
                3'd3: begin ms <= 3'd4; st <= S_IN; end
                default: begin ms <= 3'd0; emit_wide(`KJ_GETA); end
                endcase
            end
            // ------------------------------------------------ Shift_JIS
            default: begin
                esc <= 2'd0;
                if (ms == 3'd0) begin
                    if (f_sjlead) begin l1 <= cur; ms <= 3'd1; st <= S_IN; end
                    else emit_narrow(cur);                       // $A1-$DF katakana, and the others as they are
                end else if (f_sjtrail) begin
                    // row and column from the lead and trail byte (sj_a, sj_b: worked out in S_CLS)
                    if (sj_b >= 8'd94) begin ku <= {sj_a[5:0], 1'b0} + 7'd2; ten7 <= sj_b[6:0] - 7'd93; end        // b - 94 + 1
                    else               begin ku <= {sj_a[5:0], 1'b0} + 7'd1; ten7 <= sj_b[6:0] + 7'd1; end
                    ms <= 3'd0; st <= S_WIDE2;
                end else bad;
            end
            endcase
        end
        // ------------------------------------------------ a complete UTF-8 character
        S_WIDE: begin
            ms <= 3'd0;
            if (big) emit_wide(`KJ_GETA);
            else if (cp < 16'h0080) emit_narrow(cp[7:0]);
            else if (cp < 16'h00a0) st <= S_IN;                    // C1 controls: nothing
            else if (cp < 16'h0100) emit_narrow(cp[7:0]);          // Latin-1
            else if (cp >= 16'hff61 && cp <= 16'hff9f) emit_narrow(cp[7:0] - 8'h61 + 8'ha1);     // half-width katakana
            else if (!kanji_ok) emit_wide(`KJ_GETA);
            else begin x_addr <= `KJ_PSRAM + `KJ_DIR + {13'd0, cp[15:8], 1'b0}; x_req <= 1'b1; st <= S_L1W; end
        end
        S_L1W: if (x_ack) begin
            x_req <= 1'b0;
            if (x_rdata == 16'hffff) emit_wide(`KJ_GETA);
            else st <= S_L1;
            idx <= x_rdata[11:0];
        end
        S_L1: begin                                                 // pages are 512 bytes
            x_addr <= `KJ_PSRAM + `KJ_PAGES + {idx[6:0], 9'd0} + {13'd0, cp[7:0], 1'b0};
            x_req <= 1'b1; st <= S_L2W;
        end
        S_L2W: if (x_ack) begin
            x_req <= 1'b0;
            if (x_rdata == 16'hffff) emit_wide(`KJ_GETA); else emit_wide(x_rdata[11:0]);
        end
        // ------------------------------------------------ a JIS X 0208 row and column (EUC-JP, Shift_JIS)
        S_WIDE2: begin
            if (ku >= 7'd1 && ten7 >= 7'd1 && ten7 <= 7'd94 && ((ku <= 7'd8) || (ku >= 7'd16 && ku <= 7'd47)))
                emit_wide(compact(ku[5:0], ten7));
            else emit_wide(`KJ_GETA);
        end
        default: st <= S_IN;
        endcase
    end
endmodule
