// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The timer and the clock of the sim09 / sbc09 "v09" machine, registers $E030-$E036:
//
//   $E030  write $8F  start the periodic timer (and acknowledge a tick)   [FREERUN=0 only]
//          write $80  stop it, acknowledge
//          write $04  latch the date and time into $E031-$E036
//          read       the last written value; bit 4 is set by every tick (until the next write)
//   $E031-$E036  year-1900, month, day, hour, minute, second (binary), as latched by $04
//
// A tick raises the interrupt line (IRQ, or FIRQ with the monitor ROM v09.rom: FREERUN=1, the timer then
// runs from the reset on and $8F does nothing); the line goes low when the CPU takes the interrupt
// (iack) or the guest writes $8F / $80.
//
// Time is counted in E cycles of the CPU (ecyc), like the cycle driven timer of the simulator, so a
// stalled CPU (a slow disk) does not lose ticks.  The clock starts at the date given by the parameters
// (the build script passes the date of the build), there is no battery.

module timer_rtc #(
    parameter FREERUN        = 0,
    parameter ECYC_PER_TICK  = 36818,       // 50 Hz at E = 1.8409 MHz
    parameter ECYC_PER_SEC   = 1840909,
    parameter START_YEAR     = 126,         // years since 1900
    parameter START_MONTH    = 1,
    parameter START_DAY      = 1,
    parameter START_HOUR     = 0,
    parameter START_MIN      = 0,
    parameter START_SEC      = 0
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       ecyc,                 // one clk pulse per E cycle
    input  wire       wr,                   // one clk pulse: a write to $E030-$E036
    input  wire [2:0] off,                  // register 0..6 (valid with wr and for rdata)
    input  wire [7:0] wdata,
    input  wire       iack,                 // one clk pulse: the CPU acknowledges an interrupt
    output wire [7:0] rdata,
    output wire       irq
);

    reg        running;
    reg        pending;
    reg [7:0]  r0;
    reg [15:0] tcnt;

    // ---------------------------------------------------------------- the calendar
    reg [7:0] y, mo, d, h, mi, s;
    reg [10:0] slo, shi;
    reg        scarry, sec_tick;
    localparam [21:0] SEC_LAST = ECYC_PER_SEC - 1;
    reg [7:0] l [1:6];                      // the latched copy

    function [7:0] dim;                     // days in the month (years 2000-2099)
        input [7:0] yr;
        input [7:0] m;
        begin
            case (m)
                8'd2:    dim = ((yr[1:0] == 2'b00) ? 8'd29 : 8'd28);      // 1900 + yr: yr = 4k + 0  <=>  leap, since 1900 % 4 == 0
                8'd4, 8'd6, 8'd9, 8'd11: dim = 8'd30;
                default: dim = 8'd31;
            endcase
        end
    endfunction

    // the "last second / minute / hour / day / month" conditions, a clock behind (the calendar moves once a second)
    reg s59, mi59, h23, dlast, mo12;
    always @(posedge clk) begin
        s59   <= (s  == 8'd59);
        mi59  <= (mi == 8'd59);
        h23   <= (h  == 8'd23);
        mo12  <= (mo == 8'd12);
        dlast <= (d  == dim(y, mo));
    end

    always @(posedge clk) begin
        if (rst) begin
            y <= START_YEAR; mo <= START_MONTH; d <= START_DAY;
            h <= START_HOUR; mi <= START_MIN; s <= START_SEC;
            slo <= 0; shi <= 0; scarry <= 1'b0;
            sec_tick <= 1'b0;
        end else begin
            sec_tick <= 1'b0;
            scarry <= 1'b0;
            if (scarry) shi <= shi + 1'b1;
            if (ecyc) begin
                if ({shi, slo} == SEC_LAST) begin slo <= 0; shi <= 0; sec_tick <= 1'b1; end
                else begin
                    slo <= slo + 1'b1;
                    if (slo == 11'h7ff) scarry <= 1'b1;
                end
            end
            if (sec_tick) begin
                if (s59) begin
                    s <= 0;
                    if (mi59) begin
                        mi <= 0;
                        if (h23) begin
                            h <= 0;
                            if (dlast) begin
                                d <= 1;
                                if (mo12) begin mo <= 1; y <= y + 1'b1; end
                                else mo <= mo + 1'b1;
                            end else d <= d + 1'b1;
                        end else h <= h + 1'b1;
                    end else mi <= mi + 1'b1;
                end else s <= s + 1'b1;
            end
        end
    end

    // ---------------------------------------------------------------- registers and the timer
    always @(posedge clk) begin
        if (rst) begin
            running <= (FREERUN != 0);
            pending <= 1'b0;
            r0      <= 8'h00;
            tcnt    <= 16'd0;
            l[1] <= 0; l[2] <= 0; l[3] <= 0; l[4] <= 0; l[5] <= 0; l[6] <= 0;
        end else begin
            if (iack) pending <= 1'b0;
            if (running && ecyc) begin
                if (tcnt == ECYC_PER_TICK - 1) begin
                    tcnt    <= 0;
                    pending <= 1'b1;
                    r0[4]   <= 1'b1;
                end else tcnt <= tcnt + 1'b1;
            end
            if (wr) begin
                if (off == 3'd0) begin
                    if (wdata == 8'h8f) begin
                        if (FREERUN == 0) begin
                            running <= 1'b1;
                            tcnt    <= 0;
                            pending <= 1'b0;
                            r0      <= wdata;
                        end
                    end else if (wdata == 8'h80) begin
                        running <= 1'b0;
                        pending <= 1'b0;
                        r0      <= wdata;
                    end else if (wdata == 8'h04) begin
                        l[1] <= y;  l[2] <= mo; l[3] <= d;
                        l[4] <= h;  l[5] <= mi; l[6] <= s;
                    end else begin
                        r0 <= wdata;
                    end
                end else if (off <= 3'd6) begin
                    l[off] <= wdata;
                end
            end
        end
    end

    assign irq   = pending;
    assign rdata = (off == 3'd0) ? r0 :
                   (off == 3'd7) ? 8'h00 : l[off];

endmodule
