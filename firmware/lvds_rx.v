module lvds_rx (
    input       i_rst_b,
    input       i_ddr_clk,
    input [1:0] i_ddr_data,

    input             i_fifo_full,
    output            o_fifo_write_clk,
    output reg        o_fifo_push, // 'reg' added as o_fifo_push is assigned in an always block
    output reg [31:0] o_fifo_data,
    input             i_sync_input,
    output     [ 1:0] o_debug_state
);

  // Internal FSM States
  localparam state_idle = 2'b00, state_i_phase = 2'b01, state_q_phase = 2'b11;

  // Modem sync symbols
  localparam modem_i_sync = 2'b10, modem_q_sync = 2'b01;

  // Set to 1 for fixed-data-frame testing purposes only
  parameter TEST_FIXED_FRAME = 0; 

  // Internal Registers
  reg [1:0] r_state_if;
  reg [2:0] r_phase_count;
  reg r_sync_input;
  reg [4:0] r_test_cnt; // For fixed-data-frame testing purposes only

  // Initial conditions
  initial begin
    r_state_if = state_idle;
    r_phase_count = 3'b111;
    r_test_cnt = 5'b00000; // For fixed-data-frame testing purposes only
  end

  // Global Assignments
  assign o_fifo_write_clk = i_ddr_clk;
  assign o_debug_state = r_state_if;

  // Main Process
  always @(posedge i_ddr_clk or negedge i_rst_b) begin
    if (i_rst_b == 1'b0) begin
      r_state_if <= state_idle;
      o_fifo_push <= 1'b0;
      r_phase_count <= 3'b111;
      r_sync_input <= 1'b0;
      r_test_cnt <= 5'b00000; // For fixed-data-frame testing purposes only
    end else if (TEST_FIXED_FRAME) begin
      r_state_if <= state_idle;
      r_phase_count <= 3'b111;
      r_sync_input <= 1'b1;
      
      r_test_cnt <= r_test_cnt + 1'b1; // For fixed-data-frame testing purposes only
      o_fifo_push <= 1'b0; // for fixed-data-frame testing purposes only

      if ((r_test_cnt == 5'b00000) && !i_fifo_full) begin
          o_fifo_data <= {2'b10, 13'h0555, 1'b0, 2'b01, 13'h0AAA, 1'b1};
          o_fifo_push <= 1'b1;
      end //else begin
          //o_fifo_push <= 1'b0;
      //end
    end else begin
      case (r_state_if)
        state_idle: begin
          if (i_ddr_data == modem_i_sync) begin
            r_state_if   <= state_i_phase;
            o_fifo_data  <= {30'b000000000000000000000000000000, i_ddr_data};
            r_sync_input <= i_sync_input;  // mark the sync input for this sample
          end
          r_phase_count <= 3'b111;
          o_fifo_push   <= 1'b0;
        end

        state_i_phase: begin
          if (r_phase_count == 3'b000) begin
            if (i_ddr_data == modem_q_sync) begin
              r_phase_count <= 3'b110;
              r_state_if <= state_q_phase;
            end else begin
              r_state_if <= state_idle;
            end
          end else begin
            r_phase_count <= r_phase_count - 1'b1;
          end
          o_fifo_push <= 1'b0;
          o_fifo_data <= {o_fifo_data[29:0], i_ddr_data};
        end

        state_q_phase: begin
          if (r_phase_count == 3'b000) begin
            o_fifo_push <= ~i_fifo_full;
            r_state_if  <= state_idle;
            o_fifo_data <= {o_fifo_data[29:0], i_ddr_data[1], r_sync_input};
          end else begin
            o_fifo_push   <= 1'b0;
            r_phase_count <= r_phase_count - 1;
            o_fifo_data   <= {o_fifo_data[29:0], i_ddr_data};
          end
        end
      endcase
    end
  end

endmodule
