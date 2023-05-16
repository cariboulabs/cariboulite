module lvds_tx
    (
        input               i_rst_b,
        input               i_ddr_clk,
        output [1:0]        o_ddr_data,

        input               i_fifo_empty,
        output              o_fifo_read_clk,
        output              o_fifo_pull,
        input [31:0]        i_fifo_data,
        input               i_sync_input,
        output [1:0]        o_debug_state );

    // Internal Registers
    reg [4:0]   r_phase_count;

    // Initial conditions
    initial begin
        r_phase_count = 5'b11111;
    end

    assign o_fifo_read_clk = i_ddr_clk;
    

    // Main Process / shift register
    always @(posedge i_ddr_clk)
    begin
        if (i_rst_b == 1'b0) begin
            o_fifo_pull <= ~i_fifo_empty;
            r_phase_count <= 5'b11111;
        end else begin
            if (r_phase_count == 5'b00001) begin
                o_fifo_pull <= ~i_fifo_empty;
            end else begin
                o_fifo_pull <= 1'b0;
            end
            
            o_ddr_data <= i_fifo_data[r_phase_count:r_phase_count-1];        
            r_phase_count <= r_phase_count - 2;     
        end
    end

endmodule