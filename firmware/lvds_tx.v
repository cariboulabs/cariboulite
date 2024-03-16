module lvds_tx (
    input           i_rst_b,
    input           i_ddr_clk,
    output reg[1:0] o_ddr_data,

    input           i_fifo_empty,
    output          o_fifo_read_clk,
    output          o_fifo_pull,
    input [31:0]    i_fifo_data,
    input [3:0]     i_sample_gap,
    input           i_tx_state,
    input           i_sync_input,
    input           i_debug_lb,    
    output          o_tx_state_bit,
    output          o_sync_state_bit,
);

    // STATES and PARAMS
    localparam
    tx_state_sync = 1'b0,
    tx_state_tx  = 1'b1;
    localparam sync_duration_frames = 4'd10;   // at least 2.5usec
    localparam zero_frame = 32'b00000000_00000000_00000000_00000000;
    localparam lb_frame =   32'b10000100_00000011_01110000_01001000;
    
    // Internal Registers
    reg [3:0] r_sync_count;
    wire frame_pull_clock;
    wire frame_assign_clock;
    reg r_state;
    reg [3:0] r_phase_count;
    reg [31:0] r_fifo_data;
    reg r_pulled;
    reg r_schedule_zero_frame;

    // Initial conditions
    initial begin
        r_phase_count = 4'd15;
        r_fifo_data <= zero_frame;
    end

    assign o_fifo_read_clk = i_ddr_clk;
    assign o_tx_state_bit = r_state;
    assign o_sync_state_bit = 1'b0;
    assign o_fifo_pull = r_pulled;

    // SHIFT REGISTER
    always @(posedge i_ddr_clk) begin
        if (i_rst_b == 1'b0) begin
            r_phase_count <= 4'd15;
        end else begin
            o_ddr_data[1:0] <= r_fifo_data[2*r_phase_count+1 : 2*r_phase_count];
            r_phase_count <= r_phase_count - 1;
        end
    end

    // SYNC AND MANAGEMENT
    always @(posedge i_ddr_clk) 
    begin
        if (i_rst_b == 1'b0) begin
            r_state <= tx_state_sync;
            r_pulled <= 1'b0;
            r_fifo_data <= zero_frame;
            r_sync_count <= sync_duration_frames;
            r_schedule_zero_frame <= 1'b0;
        end else begin
            case (r_state)
                //----------------------------------------------
                tx_state_sync: 
                begin                
                    if (r_phase_count == 4'd0) begin
                        if (r_schedule_zero_frame) begin
                            r_fifo_data <= zero_frame;
                            r_schedule_zero_frame <= 1'b0;
                        end
                    end else if (r_phase_count == 4'd1) begin
                        if (r_sync_count == 4'd0) begin
                            if (i_debug_lb && !i_tx_state) begin
                                r_fifo_data <= lb_frame;
                                r_state <= tx_state_sync;
                            end else if (!i_debug_lb && i_tx_state) begin
                                r_pulled <= !i_fifo_empty;
                                r_state <= tx_state_tx;
                            end else begin
                                r_sync_count <= sync_duration_frames;
                                r_state <= tx_state_sync;
                                r_fifo_data <= zero_frame;
                            end
                        end else begin
                            r_sync_count <= r_sync_count - 1;
                            r_schedule_zero_frame <= 1'b1;
                            //r_fifo_data <= zero_frame;
                            r_state <= tx_state_sync;
                        end
                    end
                end

                //----------------------------------------------
                tx_state_tx: 
                begin
                    if (i_debug_lb || !i_tx_state) begin
                        r_state <= tx_state_sync;
                        r_sync_count <= sync_duration_frames;
                        r_pulled <= 1'b0;
                    end else if (r_phase_count == 4'd1) begin
                        r_pulled <= !i_fifo_empty;
                        r_state <= tx_state_tx;
                    end else if (r_phase_count == 4'd0) begin
                        if (r_pulled == 1'b0) begin
                            r_sync_count <= sync_duration_frames;
                            r_fifo_data <= zero_frame;
                            r_state <= tx_state_sync;
                        end else begin
                            r_fifo_data <= i_fifo_data;
                            if (i_sample_gap > 0) begin
                                r_sync_count <= i_sample_gap;
                                r_state <= tx_state_sync;
                            end else begin
                                r_state <= tx_state_tx;
                            end
                        end
                    
                        r_pulled <= 1'b0;
                    end
                end
            endcase
        end
    end
endmodule
