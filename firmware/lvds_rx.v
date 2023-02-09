module lvds_rx (input               i_rst_b,
				input               i_ddr_clk,
				input [1:0]         i_ddr_data,

				input               i_fifo_full,
				output              o_fifo_write_clk,
				output              o_fifo_push,
				output reg [31:0]   o_fifo_data,
				input				i_sync_input,
				output [1:0]        o_debug_state );


    // Internal FSM States
    localparam
        state_idle   = 3'b00,
        state_i_phase = 3'b01,
        state_q_phase = 3'b11;

    // Modem sync symbols
    localparam
        modem_i_sync  = 3'b10,
        modem_q_sync = 3'b01;

    // Internal Registers
    reg [1:0]   r_state_if;
    reg [3:0]   r_phase_count;

    assign o_debug_state = r_state_if;

    // Initial conditions
    initial begin
        r_state_if = state_idle;
        r_phase_count = 4'd15;
    end

    // Global Assignments
    assign o_fifo_write_clk = i_ddr_clk;

    // Main Process
    // Data structure from the modem:
    //	[S'10'] [13'I] ['0'] [S'01'] [13'Q] ['0']
    // Data structure with out sync 's'
    //	['10'] [I] ['0'] ['01'] [Q] ['s']
    always @(posedge i_ddr_clk or negedge i_rst_b)
    begin
        if (i_rst_b == 1'b0) begin
            r_state_if <= state_idle;
            r_phase_count <= 4'd15;
            o_fifo_push <= 1'b0;
        end else begin           
            case (r_state_if)
                state_idle: begin
                    if (i_ddr_data == modem_i_sync ) begin
                        r_state_if <= state_i_phase;
                    end
                    r_phase_count <= 4'd15;
                end

                state_i_phase: begin
                    if (r_phase_count == 4'd8) begin
                        if (i_ddr_data == modem_q_sync ) begin
                            r_state_if <= state_q_phase;
                        end else begin
                            r_state_if <= state_idle;
                        end
                    end
                    r_phase_count <= r_phase_count - 1;
                end

                state_q_phase: begin
                    if (r_phase_count == 4'd1) begin
                        r_state_if <= state_idle;
                    end
                    
                    r_phase_count <= r_phase_count - 1;
                end
            endcase
            
            o_fifo_data <= {o_fifo_data[29:0], i_ddr_data};
            o_fifo_push <= r_phase_count == 4'd0 && ~i_fifo_full;
        end
    end

endmodule
