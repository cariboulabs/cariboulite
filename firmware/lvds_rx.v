module lvds_rx (
    input       i_rst_b,
    input       i_ddr_clk,
    input [1:0] i_ddr_data,

    input             i_fifo_full,
    output            o_fifo_write_clk,
    output            o_fifo_push,
    output  [15:0] o_fifo_data,
    input             i_sync_input,
    output     [ 1:0] o_debug_state
);

  // Internal FSM States
  localparam state_idle = 2'b00, state_i_phase = 2'b01, state_q_sync = 2'b10, state_q_phase = 2'b11;

  // Modem sync symbols
  localparam modem_i_sync = 2'b10, modem_q_sync = 2'b01;

  // Internal Registers
  reg [1:0] r_state_if;
  reg [2:0] r_phase_count;
  reg r_sync_input;
  reg r_fifo_push;
  assign o_fifo_push = r_fifo_push;
  
  // Initial conditions
  initial begin
    r_state_if = state_idle;
    r_phase_count = 3'b111;
  end

  // Global Assignments
  assign o_fifo_write_clk = i_ddr_clk;
  assign o_debug_state = r_state_if;

  reg [35:0] r_fifo_data;
  always @(posedge i_ddr_clk) begin
    r_fifo_data  <= {r_fifo_data[33:0], i_ddr_data};;
  end
  assign o_fifo_data  = r_fifo_data[19:4];
  
  reg fifo_push2;
  // Main Process
  always @(posedge i_ddr_clk or negedge i_rst_b) begin
    if (i_rst_b == 1'b0) begin
      r_state_if <= state_idle;
      r_fifo_push <= 1'b0;
      r_phase_count <= 3'b000;
      r_sync_input <= 1'b0;
      fifo_push2 <= 1'b0;
    end else begin
      r_phase_count <= r_phase_count + 1;
      case (r_state_if)
        state_idle: begin
          if (r_fifo_data[1:0] == modem_i_sync) begin
            r_state_if   <= state_i_phase;
            
            r_sync_input <= i_sync_input;  // mark the sync input for this sample
          end 
          r_phase_count <= 3'b001;
          r_fifo_push   <= 1'b0;
          
          if(fifo_push2) begin
            r_fifo_push   <= 1'b1;
            fifo_push2 <= 1'b0;
          end
        end

        state_i_phase: begin
          if (r_phase_count == 3'b111) begin
            r_state_if <= state_q_sync;
          end
          r_fifo_push <= 1'b0;
          
        end

        state_q_sync: begin
          if (r_fifo_data[1:0] == modem_q_sync) begin
              r_state_if <= state_q_phase;
              r_fifo_push <= ~i_fifo_full;
              fifo_push2 <= ~i_fifo_full;
          end else begin
              r_state_if <= state_idle;
          end
        end
        
        state_q_phase: begin
          if (r_phase_count == 3'b111) begin
            
            r_state_if  <= state_idle;
            
          end
          r_fifo_push <= 1'b0;
        end
      endcase
    end
  end

endmodule
