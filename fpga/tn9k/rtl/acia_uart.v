// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The console port of the sim09 / sbc09 "v09" machine: a 6850 style ACIA (status at +0,
// data at +1, polled, no interrupts) on a 8N1 UART.
//
//   status: bit0 RDRF (a received character is waiting), bit1 TDRE (the transmitter can take one)
//   data:   read = the oldest received character (the last one again when none is waiting, like the
//           simulator), write = send
//
// The simulator's console is not paced, the FPGA's is: a 256 character receive FIFO keeps what
// is typed or pasted between two polls of a guest that looks at the port 50 times a second.

module acia_uart #(
    parameter BAUD_DIV = 586                // clk / baud (67.5 MHz / 115200)
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       rxd,
    output reg        txd,
    input  wire       rd_data,              // one clk pulse: the guest reads the data register
    input  wire       wr_data,              // one clk pulse: the guest writes the data register
    input  wire [7:0] wdata,
    output wire       tx_full,              // the transmitter cannot take a character now
    output wire [7:0] status,
    output wire [7:0] rdata
);

    // ---------------------------------------------------------------- receiver
    reg [1:0]  rxs;
    reg [9:0]  rbaud;
    reg [3:0]  rbit;
    reg        rbusy;
    reg [7:0]  rsh;

    reg [7:0]  fifo [0:255];
    reg [7:0]  wp, rp;
    reg [7:0]  last;
    reg        push;
    reg [7:0]  push_d;
    reg        nonempty;

    wire       empty = (wp == rp);
    reg        full;
    reg [7:0]  rq;
    always @(posedge clk) rq <= fifo[rp];                    // the head of the queue, a clock late (reads are far apart)
    always @(posedge clk) full <= ((wp + 1'b1) == rp);       // 255 characters at most (a clock late: pushes are far apart)
    wire       pop   = rd_data && !empty;

    always @(posedge clk) begin
        rxs  <= {rxs[0], rxd};
        push <= 1'b0;
        if (rst) begin
            rbusy <= 1'b0;
        end else if (!rbusy) begin
            if (!rxs[1]) begin                  // start bit
                rbusy <= 1'b1;
                rbaud <= BAUD_DIV / 2;
                rbit  <= 4'd0;
            end
        end else if (rbaud != 0) begin
            rbaud <= rbaud - 1'b1;
        end else begin
            rbaud <= BAUD_DIV - 1;
            if (rbit == 4'd0) begin             // middle of the start bit
                if (rxs[1]) rbusy <= 1'b0;      // glitch
                rbit <= 4'd1;
            end else if (rbit <= 4'd8) begin
                rsh  <= {rxs[1], rsh[7:1]};
                rbit <= rbit + 1'b1;
            end else begin                      // stop bit
                rbusy  <= 1'b0;
                push   <= 1'b1;
                push_d <= rsh;
            end
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            wp <= 8'd0; rp <= 8'd0; last <= 8'h02;
        end else begin
            if (push && !full) begin
                fifo[wp] <= push_d;
                wp <= wp + 1'b1;
            end
            if (pop) begin
                last <= rq;
                rp <= rp + 1'b1;
            end
        end
    end

    // ---------------------------------------------------------------- transmitter
    reg [7:0] thold;
    reg       tfull;
    reg [9:0] tsh;
    reg [3:0] tbit;
    reg [9:0] tbaud;
    reg       tbusy;

    always @(posedge clk) begin
        if (rst) begin
            txd <= 1'b1; tfull <= 1'b0; tbusy <= 1'b0;
        end else begin
            if (wr_data) begin
                thold <= wdata;
                tfull <= 1'b1;
            end
            if (!tbusy) begin
                if (tfull) begin
                    tsh   <= {1'b1, thold, 1'b0};
                    tbit  <= 4'd0;
                    tbaud <= BAUD_DIV - 1;
                    tbusy <= 1'b1;
                    tfull <= wr_data;           // a write in this very cycle stays pending
                    txd   <= 1'b0;
                end
            end else if (tbaud != 0) begin
                tbaud <= tbaud - 1'b1;
            end else begin
                tbaud <= BAUD_DIV - 1;
                if (tbit == 4'd9) begin
                    tbusy <= 1'b0;
                    txd   <= 1'b1;
                end else begin
                    tbit <= tbit + 1'b1;
                    tsh  <= {1'b1, tsh[9:1]};
                    txd  <= tsh[1];
                end
            end
        end
    end

    // registered: the guest reads the port at most once per bus cycle, long after a change
    reg [7:0] status_r, rdata_r;
    always @(posedge clk) begin
        status_r <= {6'b0, !tfull, !empty};
        rdata_r  <= !empty ? rq : last;
    end
    assign tx_full = tfull;
    assign status  = status_r;
    assign rdata   = rdata_r;

endmodule
