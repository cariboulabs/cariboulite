`include "spi_slave.v"

module spi_if
    (
        input               i_rst_b,          // FPGA Reset
        input               i_sys_clk,        // FPGA Clock

        output reg [4:0]    o_ioc,
        output reg [7:0]    o_data_in,          // data that was received over SPI
        input [7:0]         i_data_out,         // data to be sent over the SPI
        output reg [3:0]    o_cs,
        output reg          o_fetch_cmd,
        output reg          o_load_cmd,

        // SPI Interface
        input               i_spi_sck,
        output              o_spi_miso,
        input               i_spi_mosi,
        input               i_spi_cs_b );

    localparam
        state_idle   = 3'b000,
        state_fetch1 = 3'b001,
        state_fetch2 = 3'b010,
        state_load   = 3'b011,
        state_post_op= 3'b100;

    reg [2:0]   state_if;
    wire        w_rx_data_valid;
    wire [7:0]  w_rx_data;
    reg         r_tx_data_valid;
    reg [7:0]   r_tx_byte;

    reg [4:0]   r_ioc;
    reg [7:0]   r_data;

    // Initial conditions
   initial begin
      state_if = state_idle;
   end

    spi_slave spi
    (
        .i_sys_clk        (i_sys_clk),
        .o_rx_data_valid  (w_rx_data_valid),
        .o_rx_byte        (w_rx_data),
        .i_tx_data_valid  (r_tx_data_valid),
        .i_tx_byte        (r_tx_byte),

        .i_spi_sck        (i_spi_sck),
        .o_spi_miso       (o_spi_miso),
        .i_spi_mosi       (i_spi_mosi),
        .i_spi_cs_b       (i_spi_cs_b)
    );

    always @(posedge i_sys_clk)
    begin
        if (i_rst_b == 1'b0) begin
            state_if <= state_idle;
        // whenever a new byte arrives
        end else if (w_rx_data_valid == 1'b1) begin
            case (state_if)
                //----------------------------------
                state_idle:
                begin
                    o_ioc = w_rx_data[4:0];
                    case(w_rx_data[6:5])
                        2'b00: o_cs <= 4'b0001;
                        2'b01: o_cs <= 4'b0010;
                        2'b10: o_cs <= 4'b0100;
                        2'b11: o_cs <= 4'b1000;
                    endcase

                    if (w_rx_data[7] == 1'b1) begin     // write command
                        state_if <= state_load;
                    end else begin                      // read command
                        o_fetch_cmd <= 1'b1;
                        state_if <= state_fetch1;
                    end

                    o_load_cmd <= 1'b0;
                    r_tx_data_valid <= 1'b0;
                end
                //----------------------------------
                state_load:
                begin
                    o_data_in <= w_rx_data;
                    o_load_cmd <= 1'b1;
                    state_if <= state_idle;
                end
                //----------------------------------
                state_post_op:
                begin
                    o_fetch_cmd <= 1'b0;
                    o_load_cmd <= 1'b0;
                    state_if <= state_idle;
                end
            endcase
        end else begin
            if (state_if == state_fetch1) begin
                o_fetch_cmd <= 1'b0;
                state_if <= state_fetch2;
            end else if (state_if == state_fetch2) begin
                r_tx_byte <= i_data_out;
                r_tx_data_valid <= 1'b1;
                state_if <= state_post_op;
            end else begin
                o_load_cmd <= 1'b0;
                r_tx_data_valid <= 1'b0;
            end
        end

    end
endmodule // spi_if