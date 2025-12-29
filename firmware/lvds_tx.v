module lvds_tx (
    input           i_rst_b,
    input           i_ddr_clk,  
    input           i_fifo_empty,
    input [31:0]    i_fifo_data,
    input [3:0]     i_sample_gap,
    input           i_tx_state,
    input           i_debug_lb,
  
    output reg[1:0] o_ddr_data,
    output [2:0]    o_tx_fsm_state,
    output          o_fifo_read_clk,
    output          o_fifo_pull
);

    // STATES and PARAMS
    localparam sync_duration_frames = 4'd10;   // at least 2.5usec
    localparam zero_frame = 32'b00000000_00000000_00000000_00000000;
    localparam sync_frame = 32'b10000000_00000000_01000000_00000000;
    localparam lb_frame   = 32'b10000100_00000011_01110000_01001000;
  
    localparam IDLE      = 3'd0;
    localparam TX_FRAME  = 3'd1;
    localparam TX_GAP    = 3'd2;
    localparam LOOPBACK  = 3'd3;

    // Internal Registers
    reg [3:0]  r_sync_count;  
    reg [3:0]  r_phase_count; 
    reg [2:0]  r_state;         
    reg [31:0] r_fifo_data;   
    reg        r_pulled;      
    reg [3:0]  r_gap_frame_count;
    reg        pending_load;       // set when we asserted rd_en at last boundary
    reg        sent_first_sync;    // gate leaving IDLE until we’ve emitted one sync
    reg [3:0]  r_sample_gap;       // sampled copy of i_sample_gap (CDC-lite)
    reg        r_tx_state_q;
    reg [3:0]  next_sync; 

    wire tx_rise =  r_tx_state & ~r_tx_state_q;
    wire tx_fall = ~r_tx_state &  r_tx_state_q;
    
    wire frame_boundary = (r_phase_count == 4'd0);  
  
    assign o_fifo_read_clk = i_ddr_clk;
    assign o_fifo_pull = r_pulled;
    assign o_tx_fsm_state = r_state; // output current state for debug

    reg tx_state_d1, tx_state_d2;
    always @(posedge i_ddr_clk or negedge i_rst_b) begin
        if (!i_rst_b) {tx_state_d2, tx_state_d1} <= 2'b00;
        else {tx_state_d2, tx_state_d1} <= {tx_state_d1, i_tx_state};
    end
    wire r_tx_state = tx_state_d2;

    reg debug_lb_d1, debug_lb_d2;
    always @(posedge i_ddr_clk or negedge i_rst_b) begin
        if (!i_rst_b) {debug_lb_d2, debug_lb_d1} <= 2'b00;
        else {debug_lb_d2, debug_lb_d1} <= {debug_lb_d1, i_debug_lb};
    end
    wire r_debug_lb = debug_lb_d2;

    reg fifo_empty_d1, fifo_empty_d2;
    always @(posedge i_ddr_clk or negedge i_rst_b) begin
        if (!i_rst_b) {fifo_empty_d2, fifo_empty_d1} <= 2'b11;
        else {fifo_empty_d2, fifo_empty_d1} <= {fifo_empty_d1, i_fifo_empty};
    end
    wire r_fifo_empty = fifo_empty_d2;
    
    // SHIFT REGISTER and STATE MACHINE
    always @(posedge i_ddr_clk or negedge i_rst_b) begin
        if (i_rst_b == 1'b0) begin
            r_phase_count     <= 4'd15;
            r_sync_count      <= sync_duration_frames;
            r_fifo_data       <= zero_frame;
            r_state           <= INIT;
            r_pulled          <= 1'b0;
            r_gap_frame_count <= 4'd0;
            pending_load      <= 1'b0;
            sent_first_sync   <= 1'b0;
            r_sample_gap      <= 4'd0;
            o_ddr_data        <= 2'b00;
            r_tx_state_q      <= 1'b0;
              
        end else begin
            
            // SHIFT REGISTER
            o_ddr_data[1:0] <= r_fifo_data[2*r_phase_count+1 : 2*r_phase_count];
            // case (r_phase_count)
            //       15: o_ddr_data[1:0] <= r_fifo_data[31:30];
            //       14: o_ddr_data[1:0] <= r_fifo_data[29:28];
            //       13: o_ddr_data[1:0] <= r_fifo_data[27:26];
            //       12: o_ddr_data[1:0] <= r_fifo_data[25:24];
            //       11: o_ddr_data[1:0] <= r_fifo_data[23:22];
            //       10: o_ddr_data[1:0] <= r_fifo_data[21:20];
            //       9:  o_ddr_data[1:0] <= r_fifo_data[19:18];
            //       8:  o_ddr_data[1:0] <= r_fifo_data[17:16];
            //       7:  o_ddr_data[1:0] <= r_fifo_data[15:14];
            //       6:  o_ddr_data[1:0] <= r_fifo_data[13:12];
            //       5:  o_ddr_data[1:0] <= r_fifo_data[11:10];
            //       4:  o_ddr_data[1:0] <= r_fifo_data[ 9: 8];
            //       3:  o_ddr_data[1:0] <= r_fifo_data[ 7: 6];
            //       2:  o_ddr_data[1:0] <= r_fifo_data[ 5: 4];
            //       1:  o_ddr_data[1:0] <= r_fifo_data[ 3: 2];
            //       0:  o_ddr_data[1:0] <= r_fifo_data[ 1: 0];
            //       default: o_ddr_data[1:0] <= o_ddr_data[1:0]; // keep the last value
            // endcase
            
            // default: deassert pull unless we decide at boundary
            r_pulled <= 1'b0;
            r_tx_state_q <= r_tx_state;

            // --- handle everything at frame boundaries ---
            if (frame_boundary) begin
                // sample config (cheap CDC – OK for slow register writes)
                r_sample_gap <= i_sample_gap;

                // handoff the FIFO data one boundary after we pulled it
                if (pending_load) begin
                    r_fifo_data  <= i_fifo_data;
                    pending_load <= 1'b0;
                end

                // idle cadence counter rolls 9→...→0 then reloads to 10
                //r_sync_count <= (r_sync_count == 4'd0) ? sync_duration_frames : (r_sync_count - 1'b1);
                
                // If TX is off, require a new sync burst before leaving IDLE next time
                //if (!r_tx_state) sent_first_sync <= 1'b0;

                // default next value for the idle cadence
                next_sync = (r_sync_count == 4'd0) ? sync_duration_frames : (r_sync_count - 1'b1);

                // On TX disable: re-arm preamble and kill any pending load
                if (tx_fall) begin
                    sent_first_sync <= 1'b0;   // must see a fresh sync before leaving IDLE
                    pending_load    <= 1'b0;   // cancel staged FIFO handoff
                    next_sync       <= 4'd1;   // schedule: one zero frame then a sync soon
                // optional: realign the shifter to a frame boundary
                // r_phase_count   <= 4'd15;
                end

                // On TX enable: ensure a quick sync
                if (tx_rise) begin
                next_sync <= 4'd1;         // zero frame, then sync
                end

                r_sync_count <= next_sync;

                // STATE MACHINE
                case (r_state)
                    //---------------------------//
                    IDLE: begin
                        // emit idle pattern
                        if (r_sync_count == 4'd0) begin
                            r_fifo_data      <= sync_frame;
                            sent_first_sync  <= 1'b1;
                        end else begin
                            r_fifo_data <= zero_frame;
                        end
                         
                        // only leave IDLE after first sync and with data ready
                        if (!r_debug_lb && r_tx_state && sent_first_sync && !r_fifo_empty) begin
                            r_pulled     <= 1'b1;   // request next word now
                            pending_load <= 1'b1;   // latch it next boundary
                            if (r_sample_gap == 4'd0) begin
                                r_state <= TX_FRAME;
                            end else begin
                                r_state <= TX_GAP;
                                r_gap_frame_count <= r_sample_gap - 1'b1;
                            end
                        end else if (r_debug_lb) begin
                            r_state <= LOOPBACK;
                        end  
                    end

                    //---------------------------//
                    TX_FRAME: begin
                        // currently outputting a data frame (loaded last boundary)
                        if (r_sample_gap == 4'd0) begin
                            if (r_tx_state && !r_debug_lb && !r_fifo_empty) begin
                                r_pulled     <= 1'b1;
                                pending_load <= 1'b1;    // pipeline next data frame
                                r_state      <= TX_FRAME;
                            end else begin
                                r_fifo_data  <= zero_frame;   // ensure immediate return-to-idle zeros
                                r_state <= IDLE;              // back to idle/sync
                            end
                        end else begin
                            r_state <= TX_GAP;           // schedule gaps after this data frame
                            r_gap_frame_count <= r_sample_gap - 1'b1;
                        end
                    end

                    //---------------------------//
                    TX_GAP: begin
                        // emit zero frames during the gap
                        r_fifo_data <= zero_frame;
                        if (r_gap_frame_count != 0) begin
                            r_gap_frame_count <= r_gap_frame_count - 1'b1;
                        end else begin
                            if (r_tx_state && !r_debug_lb && !r_fifo_empty) begin
                                r_pulled     <= 1'b1;
                                pending_load <= 1'b1;
                                r_state      <= TX_FRAME;
                            end else begin
                                r_state <= IDLE;
                            end
                        end
                    end
                    //---------------------------//
                    LOOPBACK: begin
                        r_fifo_data <= lb_frame;
                        if (!r_debug_lb) r_state <= IDLE;
                    end
                    //---------------------------//
                    // other states go below

                    // other states go above
                    default: r_state <= IDLE;
                endcase
            end

            // free-running phase counter (wrap 0→15)
            r_phase_count <= r_phase_count - 1'b1; 
        end // phase loop
    end // always blobk
endmodule
