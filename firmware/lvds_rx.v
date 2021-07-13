module lvds_rx
    (
        input               i_reset,
        input               i_ddr_clk,
        input [1:0]         i_ddr_data,

        input               i_fifo_full,
        output              o_fifo_write_clk,
        output              o_fifo_push,
        output [31:0]       o_fifo_data );

    // Internal FSM States
    localparam
        state_idle   = 3'b000,
        state_i_phase = 3'b001,
        state_q_phase = 3'b010;

    // Internal Registers
    reg [2:0]   r_state_if;
    reg [2:0]   r_phase_count;
    reg [31:0]  r_data;
    reg         r_push;


    // Initial conditions
    initial begin
        r_state_if = state_idle;
        r_push = 1'b0;
        r_phase_count = 3'b111;
        r_data = 0;
    end

    // Global Assignments
    assign o_fifo_push = r_push;
    assign o_fifo_data = r_data;
    assign o_fifo_write_clk = i_ddr_clk;

    // Main Process
    always @(negedge i_ddr_clk)
    begin
        if (i_reset) begin
            r_state_if = state_idle;
            r_push = 1'b0;
            r_phase_count = 3'b111;
            r_data = 0;
        end else begin
            case (r_state_if)
                state_idle: begin
                    if (i_ddr_data == 2'b10 ) begin
                        r_state_if <= state_i_phase;
                        r_data <= 0; // may be redundant as we do a full loop
                    end
                    r_phase_count <= 3'b111;
                    r_push <= 1'b0;
                end

                state_i_phase: begin
                    if (r_phase_count == 3'b000) begin
                        r_phase_count <= 3'b111;
                        r_state_if <= state_q_phase;
                        r_data <= {r_data[29:0], 2'b00};
                    end else begin
                        r_phase_count <= r_phase_count - 1;
                        r_data <= {r_data[29:0], i_ddr_data};
                    end
                end

                state_q_phase: begin
                    if (r_phase_count == 3'b001) begin
                        r_push <= ~i_fifo_full;
                        r_phase_count <= 3'b000;
                        r_state_if <= state_idle;
                    end else begin
                        r_phase_count <= r_phase_count - 1;
                    end
                    r_data <= {r_data[29:0], i_ddr_data};
                end
            endcase
        end


    end

endmodule // smi_ctrl