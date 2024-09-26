//`include "async_fifo_ctrl_fwft.v"
module sync_fifo_fwft #(	
	parameter ADDR_WIDTH = 10,
	parameter DATA_WIDTH = 16 
)
(
	input wire 			            clk,
	input wire 			            reset_n,
	
	input wire 			            wr_en_i,
	input wire [DATA_WIDTH-1:0]	wr_data_i,


	input wire 			            rd_en_i,
	output reg [DATA_WIDTH-1:0]	rd_data_o,

	output reg 						full_o,
	output reg 						empty_o,

);


	wire [ADDR_WIDTH-1:0] rd_addr_mem;
	wire [ADDR_WIDTH-1:0] wr_addr_mem;
	
	
	reg [ADDR_WIDTH-1:0]	wr_addr;
	reg [ADDR_WIDTH-1:0]	rd_addr;


	always @(posedge clk) begin
		if (reset_n == 1'b0) begin
			wr_addr <= 0;
		end else if (wr_en_i) begin
			wr_addr <= wr_addr + 1'b1;
		end
	end


	always @(posedge clk) begin
		if (reset_n == 1'b0) begin
			full_o <= 0;
		end else if (wr_en_i) begin
			full_o <= (wr_addr + 2'd2) == rd_addr;
		end else begin
			full_o <= (wr_addr + 2'd1) == rd_addr;
		end
	end

	always @(posedge clk) begin
		if (reset_n == 1'b0) begin
			rd_addr <= 0;
			
		end else if (rd_en_i) begin
			rd_addr <= rd_addr + 1'b1;
		end
	end


	always @(posedge clk) begin
		if (reset_n == 1'b0) begin
			empty_o <= 1'b1;
		end else if (rd_en_i) begin
			empty_o <= (rd_addr + 1'd1) == wr_addr;
		end else begin
			empty_o <= (rd_addr) == wr_addr;
		end
	end

	assign rd_addr_mem = (rd_en_i)? rd_addr +1'd1 : rd_addr;
	assign wr_addr_mem = wr_addr;
	
	
	// ////
	// MEMORY OPERATIONS
	// ////
	always @(posedge clk) begin
		rd_data_o <= mem_iq[rd_addr_mem];
		
	end
		
	always @(posedge clk) begin
		if (wr_en_i) begin
				mem_iq[wr_addr_mem] <= wr_data_i;
		end
	end

	reg [(DATA_WIDTH)-1:0] mem_iq[(1<<(ADDR_WIDTH))-1:0];

endmodule
