// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// A text terminal for the HDMI display: it takes the bytes the guest writes to the console and keeps a
// COLS x ROWS screen of (character, colour) cells in a video RAM that the display reads.
//
//   printable bytes   written at the cursor, which moves right; the line wraps when the next character comes
//                     (bytes >= $80 are glyph numbers as they are: the half-width katakana of Shift_JIS)
//   CR LF BS TAB FF   the usual (LF scrolls the scroll region at its bottom line)
//   ESC [ ...         A B C D (cursor), H f (position), J K (erase), L M (insert, delete lines), r (scroll region),
//                     m (colours, bold, reverse), s u (save, restore cursor)
//   ESC 7 8 D E M c   save, restore cursor; index, next line, reverse index; reset
//
// This is what jwp (docs/JWP.md) and the FUZIX / OS-9 shells need.  The video RAM has one word per cell:
// [7:0] the character, [15:8] the colours (bg[3:0] fg[3:0], ANSI order 0 black 1 red 2 green 3 yellow 4 blue
// 5 magenta 6 cyan 7 white, +8 bright).  It is kept in two copies that are written together: one is read by
// the display, the other by the scroll and insert operations here (vr_*).

module hdmi_term #(
    parameter COLS = 80,
    parameter ROWS = 25
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        stb,                 // one clk pulse: a byte for the screen
    input  wire [11:0] din,                 // a narrow character (the byte in din[7:0]) or, with wide, a kanji glyph number
    input  wire        wide,
    output reg         vw_en,
    output reg  [10:0] vw_addr,
    output reg  [23:0] vw_data,
    output reg  [10:0] vr_addr,
    input  wire [23:0] vr_data,             // the word at vr_addr, one clock later
    output wire [6:0]  cur_col,
    output wire [4:0]  cur_row,
    output wire        busy                 // the queue is nearly full
);

    localparam [7:0] DEF_ATTR = 8'h07;      // white on black
    localparam [4:0] LASTROW  = ROWS - 1;
    localparam [6:0] LASTCOL  = COLS - 1;

    // ---------------------------------------------------------------- input queue
    reg [12:0] fifo [0:255];
    reg [7:0] wp, rp;
    always @(posedge clk) begin
        if (rst) wp <= 8'd0;
        else if (stb && (wp + 1'b1 != rp)) begin fifo[wp] <= {wide, din}; wp <= wp + 1'b1; end
    end
    assign busy = (wp - rp) >= 8'd192;

    // ---------------------------------------------------------------- state
    localparam [4:0]
        ST_INIT = 5'd0,  ST_IDLE = 5'd1,  ST_DEC  = 5'd2,  ST_PUT  = 5'd3,  ST_PUT2 = 5'd4,
        ST_LF   = 5'd5,  ST_ADDR = 5'd6,  ST_ADDR2 = 5'd7, ST_CSI  = 5'd8,  ST_SGR  = 5'd9,
        ST_FILL = 5'd10, ST_SCR  = 5'd11, ST_COPY0 = 5'd12, ST_COPY1 = 5'd13, ST_COPY2 = 5'd14,
        ST_SCRF = 5'd15, ST_REP  = 5'd16, ST_RI   = 5'd17, ST_ERASE = 5'd18, ST_FIX0 = 5'd19,
        ST_FIX1 = 5'd20, ST_FIX2 = 5'd21, ST_FIX3 = 5'd22, ST_FIX4 = 5'd23, ST_FIX5 = 5'd24, ST_PUT3 = 5'd25;
    localparam [1:0] P_NORM = 2'd0, P_ESC = 2'd1, P_CSI = 2'd2, P_PCT = 2'd3;      // P_PCT: after ESC %, the next byte selects the character set (the decoder has used it)

    reg [4:0]  st, ret, fret;
    reg [1:0]  ps;
    reg [4:0]  row, top, bot, srow;
    reg [6:0]  col, scol;
    reg        wrap;
    reg [10:0] ca, rb;
    reg [7:0]  b;
    reg        bw;                          // the byte is a wide character: wi is its glyph number
    reg [11:0] wi;
    reg [7:0]  attr;
    reg [3:0]  fg, bg;
    reg        bold, rev;
    reg [7:0]  pr0, pr1, pr2, pr3;
    reg [1:0]  pi;
    reg [1:0]  si;
    reg        priv;
    reg [7:0]  cmd;
    reg [7:0]  rep;
    // fill / copy / scroll parameters
    reg [10:0] fa, fn, cs, cd, cn;
    reg        cdown, s_up;
    reg [4:0]  s_r0, s_r1;
    reg [7:0]  cv_attr;

    assign cur_col = col;
    assign cur_row = row;

    function [10:0] mul80;
        input [4:0] r;
        begin
            mul80 = ({6'd0, r} << 6) + ({6'd0, r} << 4);
        end
    endfunction

    wire [7:0] n1 = (pr0 == 8'd0) ? 8'd1 : pr0;                     // a count with the default 1
    wire [3:0] fg_eff = bold && !fg[3] ? {1'b1, fg[2:0]} : fg;
    wire [7:0] attr_eff = rev ? {fg_eff, bg} : {bg, fg_eff};

    reg  [7:0]  pr_cur;
    wire [11:0] pr_next = {pr_cur, 3'b000} + {pr_cur, 1'b0} + {8'd0, b[3:0]};
    always @(*) begin
        case (pi)
            2'd0: pr_cur = pr0;
            2'd1: pr_cur = pr1;
            2'd2: pr_cur = pr2;
            default: pr_cur = pr3;
        endcase
    end
    wire [7:0] pr_sat = pr_next[11:8] != 4'd0 ? 8'd255 : pr_next[7:0];
    reg  [7:0] sgr;
    always @(*) begin
        case (si)
            2'd0: sgr = pr0;
            2'd1: sgr = pr1;
            2'd2: sgr = pr2;
            default: sgr = pr3;
        endcase
    end

    wire [4:0]  rowp1 = row + 1'b1;

    always @(posedge clk) begin
        vw_en <= 1'b0;
        if (rst) begin
            st <= ST_INIT; ps <= P_NORM; row <= 0; col <= 0; wrap <= 1'b0; top <= 0; bot <= LASTROW;
            fg <= 4'd7; bg <= 4'd0; bold <= 1'b0; rev <= 1'b0; attr <= DEF_ATTR; rp <= 8'd0;
            srow <= 0; scol <= 0; cv_attr <= DEF_ATTR;
            fa <= 0; fn <= COLS * ROWS; fret <= ST_ADDR; ret <= ST_IDLE;
        end else begin
            attr <= attr_eff;
            case (st)
            // ------------------------------------------------ clear the screen after a reset
            ST_INIT: begin
                st <= ST_FILL;
            end
            ST_IDLE: if (wp != rp) begin
                b  <= fifo[rp][7:0];
                wi <= fifo[rp][11:0];
                bw <= fifo[rp][12];
                rp <= rp + 1'b1;
                st <= ST_DEC;
            end
            // ------------------------------------------------ decode one byte
            ST_DEC: begin
                st <= ST_IDLE;
                if (bw) begin ps <= P_NORM; st <= ST_PUT; end
                else case (ps)
                P_NORM: begin
                    if (b == 8'h1b) ps <= P_ESC;
                    else if (b == 8'h0a || b == 8'h0b) begin ret <= ST_IDLE; st <= ST_LF; end
                    else if (b == 8'h0d) begin col <= 0; wrap <= 1'b0; ret <= ST_IDLE; st <= ST_ADDR; end
                    else if (b == 8'h08) begin if (col != 0) col <= col - 1'b1; wrap <= 1'b0; ret <= ST_IDLE; st <= ST_ADDR; end
                    else if (b == 8'h09) begin col <= (col[6:3] == LASTCOL[6:3]) ? LASTCOL : {col[6:3] + 1'b1, 3'b000}; wrap <= 1'b0; ret <= ST_IDLE; st <= ST_ADDR; end
                    else if (b == 8'h0c) begin   // form feed: clear the screen and go home
                        row <= 0; col <= 0; wrap <= 1'b0; fa <= 0; fn <= COLS * ROWS; cv_attr <= attr; fret <= ST_ADDR; ret <= ST_IDLE; st <= ST_FILL;
                    end
                    else if (b >= 8'h20 && b != 8'h7f) st <= ST_PUT;
                end
                P_ESC: begin
                    ps <= P_NORM;
                    case (b)
                    8'h5b: begin ps <= P_CSI; pr0 <= 0; pr1 <= 0; pr2 <= 0; pr3 <= 0; pi <= 0; priv <= 1'b0; end   // [
                    8'h37: begin srow <= row; scol <= col; end                                                    // 7
                    8'h38: begin row <= srow; col <= scol; wrap <= 1'b0; ret <= ST_IDLE; st <= ST_ADDR; end        // 8
                    8'h44: begin ret <= ST_IDLE; st <= ST_LF; end                                                  // D  index
                    8'h45: begin col <= 0; wrap <= 1'b0; ret <= ST_IDLE; st <= ST_LF; end                          // E  next line
                    8'h4d: st <= ST_RI;                                                                           // M  reverse index
                    8'h63: begin   // c: reset
                        row <= 0; col <= 0; wrap <= 1'b0; top <= 0; bot <= LASTROW; fg <= 4'd7; bg <= 4'd0; bold <= 1'b0; rev <= 1'b0;
                        cv_attr <= DEF_ATTR; fa <= 0; fn <= COLS * ROWS; fret <= ST_ADDR; ret <= ST_IDLE; st <= ST_FILL;
                    end
                    8'h25: ps <= P_PCT;
                    8'h1b: ps <= P_ESC;
                    default: ;
                    endcase
                end
                P_CSI: begin
                    if (b >= 8'h30 && b <= 8'h39) begin
                        case (pi)
                        2'd0: pr0 <= pr_sat;
                        2'd1: pr1 <= pr_sat;
                        2'd2: pr2 <= pr_sat;
                        default: pr3 <= pr_sat;
                        endcase
                    end
                    else if (b == 8'h3b) begin if (pi != 2'd3) pi <= pi + 1'b1; end
                    else if (b == 8'h3f || b == 8'h3c || b == 8'h3d || b == 8'h3e) priv <= 1'b1;
                    else if (b >= 8'h40 && b <= 8'h7e) begin ps <= P_NORM; cmd <= b; st <= ST_CSI; end
                    else if (b == 8'h1b) ps <= P_ESC;
                    else if (b == 8'h18 || b == 8'h1a) ps <= P_NORM;
                    else ps <= P_CSI;               // intermediates and controls inside a sequence: ignored
                end
                default: ps <= P_NORM;
                endcase
            end
            // ------------------------------------------------ a character
            ST_PUT: begin
                if (wrap || (bw && col == LASTCOL)) begin col <= 0; wrap <= 1'b0; ret <= ST_FIX0; st <= ST_LF; end
                else st <= ST_FIX0;
            end
            // before a character is written, a half of a wide character that it covers must not be left alone
            ST_FIX0: begin vr_addr <= ca; st <= ST_FIX1; end
            ST_FIX1: st <= ST_FIX2;
            ST_FIX2: begin
                st <= bw ? ST_FIX3 : ST_PUT2;
                if (vr_data[15:12] == 4'd2 && col != 0) begin             // a right half: its left half goes
                    vw_en <= 1'b1; vw_addr <= ca - 1'b1; vw_data <= {attr, 16'h0020};
                end else if (vr_data[15:12] == 4'd1 && !bw && col != LASTCOL) begin   // a left half: its right half goes
                    vw_en <= 1'b1; vw_addr <= ca + 1'b1; vw_data <= {attr, 16'h0020};
                end
            end
            ST_FIX3: begin vr_addr <= ca + 1'b1; st <= ST_FIX4; end
            ST_FIX4: st <= ST_FIX5;
            ST_FIX5: begin
                st <= ST_PUT2;
                if (vr_data[15:12] == 4'd1 && col + 2'd2 <= LASTCOL) begin    // a left half at col+1: the right half at col+2 goes
                    vw_en <= 1'b1; vw_addr <= ca + 2'd2; vw_data <= {attr, 16'h0020};
                end
            end
            ST_PUT2: begin
                vw_en   <= 1'b1;
                vw_addr <= ca;
                if (bw) begin
                    vw_data <= {attr, 4'h1, wi};
                    st <= ST_PUT3;
                end else begin
                    vw_data <= {attr, 8'h00, b};
                    if (col == LASTCOL) wrap <= 1'b1;
                    else col <= col + 1'b1;
                    ret <= ST_IDLE; st <= ST_ADDR;
                end
            end
            ST_PUT3: begin                                                // the right half of a wide character
                vw_en   <= 1'b1;
                vw_addr <= ca + 1'b1;
                vw_data <= {attr, 4'h2, wi};
                if (col + 1'b1 == LASTCOL) begin col <= LASTCOL; wrap <= 1'b1; end
                else col <= col + 2'd2;
                ret <= ST_IDLE; st <= ST_ADDR;
            end
            // ------------------------------------------------ line feed (index): scroll at the bottom of the region
            ST_LF: begin
                wrap <= 1'b0;
                if (row == bot) begin
                    s_up <= 1'b1; s_r0 <= top; s_r1 <= bot; rep <= 8'd1; fret <= ST_ADDR; st <= ST_SCR;
                end else begin
                    if (row != LASTROW) row <= rowp1;
                    st <= ST_ADDR;
                end
            end
            ST_RI: begin
                wrap <= 1'b0;
                if (row == top) begin
                    s_up <= 1'b0; s_r0 <= top; s_r1 <= bot; rep <= 8'd1; ret <= ST_IDLE; fret <= ST_ADDR; st <= ST_SCR;
                end else begin
                    if (row != 0) row <= row - 1'b1;
                    ret <= ST_IDLE; st <= ST_ADDR;
                end
            end
            // ------------------------------------------------ the address of the cursor
            ST_ADDR:  begin rb <= mul80(row); st <= ST_ADDR2; end
            ST_ADDR2: begin ca <= rb + {4'd0, col}; st <= ret; end
            // ------------------------------------------------ CSI commands
            ST_CSI: begin
                st <= ST_ADDR; ret <= ST_IDLE;
                cv_attr <= attr;
                if (priv) begin
                    st <= ST_IDLE;                      // ESC [ ? ... : modes are ignored
                end else case (cmd)
                8'h41: begin row <= (row >= n1[4:0] && n1 < 8'd32) ? row - n1[4:0] : 5'd0; wrap <= 1'b0; end              // A up
                8'h42: begin row <= (n1 < 8'd32 && ({1'b0, row} + n1[5:0]) < ROWS) ? row + n1[4:0] : LASTROW; wrap <= 1'b0; end // B down
                8'h43: begin col <= (n1 < 8'd128 && ({1'b0, col} + n1[7:0]) < COLS) ? col + n1[6:0] : LASTCOL; wrap <= 1'b0; end // C right
                8'h44: begin col <= (col >= n1[6:0] && n1 < 8'd128) ? col - n1[6:0] : 7'd0; wrap <= 1'b0; end              // D left
                8'h48, 8'h66: begin                                                                                     // H f
                    row <= (pr0 == 0) ? 5'd0 : (pr0 > ROWS ? LASTROW : pr0[4:0] - 1'b1);
                    col <= (pr1 == 0) ? 7'd0 : (pr1 > COLS ? LASTCOL : pr1[6:0] - 1'b1);
                    wrap <= 1'b0;
                end
                8'h4a: begin                                                                                            // J
                    st <= ST_ERASE;
                end
                8'h4b: st <= ST_ERASE;                                                                                  // K
                8'h4c, 8'h4d: begin                                                                                     // L M
                    if (row >= top && row <= bot) begin
                        s_up <= (cmd == 8'h4d); s_r0 <= row; s_r1 <= bot;
                        rep <= (n1 > {3'd0, bot - row} + 8'd1) ? {3'd0, bot - row} + 8'd1 : n1; fret <= ST_REP; col <= 0; wrap <= 1'b0; st <= ST_SCR;
                    end
                end
                8'h6d: begin si <= 2'd0; st <= ST_SGR; end                                                              // m
                8'h72: begin                                                                                            // r
                    if (pr0 == 0 && pr1 == 0) begin top <= 0; bot <= LASTROW; end
                    else if ((pr0 == 0 ? 8'd1 : pr0) < (pr1 == 0 ? ROWS : pr1) && (pr1 == 0 ? ROWS : pr1) <= ROWS) begin
                        top <= (pr0 == 0) ? 5'd0 : pr0[4:0] - 1'b1;
                        bot <= (pr1 == 0) ? LASTROW : pr1[4:0] - 1'b1;
                    end
                    row <= 0; col <= 0; wrap <= 1'b0;
                end
                8'h73: begin srow <= row; scol <= col; st <= ST_IDLE; end                                               // s
                8'h75: begin row <= srow; col <= scol; wrap <= 1'b0; end                                                // u
                default: st <= ST_IDLE;
                endcase
            end
            ST_ERASE: begin                              // J and K: the area to fill
                st <= ST_FILL; fret <= ST_ADDR; ret <= ST_IDLE;
                if (cmd == 8'h4a) begin
                    case (pr0)
                    8'd0: begin fa <= ca; fn <= COLS * ROWS - ca; end
                    8'd1: begin fa <= 11'd0; fn <= ca + 1'b1; end
                    default: begin fa <= 11'd0; fn <= COLS * ROWS; end
                    endcase
                end else begin
                    case (pr0)
                    8'd0: begin fa <= ca; fn <= COLS - col; end
                    8'd1: begin fa <= rb; fn <= {4'd0, col} + 1'b1; end
                    default: begin fa <= rb; fn <= COLS; end
                    endcase
                end
            end
            ST_SGR: begin
                if (sgr == 8'd0) begin fg <= 4'd7; bg <= 4'd0; bold <= 1'b0; rev <= 1'b0; end
                else if (sgr == 8'd1) bold <= 1'b1;
                else if (sgr == 8'd22) bold <= 1'b0;
                else if (sgr == 8'd7) rev <= 1'b1;
                else if (sgr == 8'd27) rev <= 1'b0;
                else if (sgr >= 8'd30 && sgr <= 8'd37) fg <= {1'b0, sgr[2:0] - 3'd6};                    // 30 = %0011110: low bits 6
                else if (sgr == 8'd39) fg <= 4'd7;
                else if (sgr >= 8'd40 && sgr <= 8'd47) bg <= {1'b0, sgr[2:0]};                           // 40 = %0101000: low bits 0
                else if (sgr == 8'd49) bg <= 4'd0;
                else if (sgr >= 8'd90 && sgr <= 8'd97) begin fg <= {1'b0, sgr[2:0] - 3'd2}; bold <= 1'b1; end   // 90 = %1011010: low bits 2
                if (si == pi) st <= ST_IDLE;
                else si <= si + 1'b1;
            end
            // ------------------------------------------------ fill fn cells from fa with a blank
            ST_FILL: begin
                if (fn == 0) st <= fret;
                else begin
                    vw_en   <= 1'b1;
                    vw_addr <= fa;
                    vw_data <= {cv_attr, 16'h0020};
                    fa <= fa + 1'b1;
                    fn <= fn - 1'b1;
                end
            end
            // ------------------------------------------------ shift the rows s_r0..s_r1 up or down by one and clear the free row
            ST_SCR: begin
                cv_attr <= attr;
                if (s_up) begin                          // rows r0+1..r1 -> r0..r1-1
                    cs <= mul80(s_r0) + COLS; cd <= mul80(s_r0); cdown <= 1'b0;
                    cn <= mul80(s_r1) - mul80(s_r0);
                    fa <= mul80(s_r1);
                end else begin                           // rows r0..r1-1 -> r0+1..r1, from the end
                    cs <= mul80(s_r1) - 11'd1; cd <= mul80(s_r1) + COLS - 11'd1; cdown <= 1'b1;
                    cn <= mul80(s_r1) - mul80(s_r0);
                    fa <= mul80(s_r0);
                end
                fn <= COLS;
                st <= ST_COPY0;
            end
            ST_COPY0: begin
                if (cn == 0) st <= ST_SCRF;
                else begin vr_addr <= cs; st <= ST_COPY1; end
            end
            ST_COPY1: st <= ST_COPY2;                    // the word is on its way
            ST_COPY2: begin
                vw_en   <= 1'b1;
                vw_addr <= cd;
                vw_data <= vr_data;
                cn <= cn - 1'b1;
                if (cdown) begin cs <= cs - 1'b1; cd <= cd - 1'b1; end
                else       begin cs <= cs + 1'b1; cd <= cd + 1'b1; end
                st <= ST_COPY0;
            end
            ST_SCRF: begin st <= ST_FILL; end            // then clear the free row; fret says what comes next
            ST_REP: begin                                // L and M: n times
                if (rep > 8'd1) begin rep <= rep - 1'b1; st <= ST_SCR; end
                else begin ret <= ST_IDLE; st <= ST_ADDR; end
            end
            default: st <= ST_IDLE;
            endcase
        end
    end

endmodule
