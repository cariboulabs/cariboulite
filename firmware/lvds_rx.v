module lvds_rx
    (
        input               i_reset,
        input               i_ddr_clk,
        input [1:0]         i_ddr_data,

        input               i_fifo_full,
        output              o_fifo_write_clk,
        output              o_fifo_push,
        output reg [31:0]   o_fifo_data,
        output [1:0]        o_debug_state );

    // Internal FSM States
    localparam
        state_idle   = 3'b00,
        state_i_phase = 3'b01,
        state_q_phase = 3'b10;

    // Modem sync symbols
    localparam
        modem_i_sync  = 3'b10,
        modem_q_sync = 3'b01;

    // Internal Registers
    reg [1:0]   r_state_if;
    reg [2:0]   r_phase_count;
    reg [31:0]  r_data;
    reg         r_push;
    reg [3:0]   r_cnt;

    assign o_debug_state = r_state_if;

    // Initial conditions
    initial begin
        r_state_if = state_idle;
        r_push = 1'b0;
        r_phase_count = 3'b111;
        r_data = 0;
    end

    // Global Assignments
    assign o_fifo_push = r_push;
    //assign o_fifo_data = r_data;
    assign o_fifo_write_clk = i_ddr_clk;

    // Main Process
    always @(negedge i_ddr_clk)
    begin
        if (i_reset) begin
            r_state_if = state_idle;
            r_push = 1'b0;
            r_phase_count = 3'b111;
            r_data = 0;
            r_cnt = 0;
        end else begin
            case (r_state_if)
                state_idle: begin
                    if (i_ddr_data == 2'b10 ) begin
                        r_state_if <= state_i_phase;
                        r_data[31:2] <= 0;
                        r_data[1:0] <= r_cnt[3:2];
                    end

                    if (r_push == 1'b1) begin
                        o_fifo_data <= r_data;
                    end

                    r_phase_count <= 3'b111;
                    r_push <= 1'b0;
                end

                state_i_phase: begin
                    if (r_phase_count == 3'b000) begin
                        if (i_ddr_data == 2'b01 ) begin
                            r_phase_count <= 3'b110;
                            r_state_if <= state_q_phase;
                            r_data <= {r_data[29:0], r_cnt[1:0]};
                        end else begin
                            r_state_if <= state_idle;
                        end

                    end else begin
                        r_phase_count <= r_phase_count - 1;
                        r_data <= {r_data[29:0], i_ddr_data};
                        //r_data <= {r_data[29:0], 2'b10};
                    end
                end

                state_q_phase: begin
                    if (r_phase_count == 3'b000) begin
                        r_push <= ~i_fifo_full;
                        r_state_if <= state_idle;
                        r_cnt <= r_cnt + 1;
                    end else begin
                        r_phase_count <= r_phase_count - 1;
                    end
                    r_data <= {r_data[29:0], i_ddr_data};
                    //r_data <= {r_data[29:0], 2'b01};
                end
            endcase
        end


    end

endmodule // smi_ctrl