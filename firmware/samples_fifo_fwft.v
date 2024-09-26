
/*

	1) write endianizer
	2) async
	3) fwft
	4) one stall between writes and one between reads

 */
module samples_fifo_fwft #(	
	parameter ADDR_WIDTH = 10,
	parameter DATA_WIDTH = 16 
)
(
	input wire 			            wr_rst_b_i,
	input wire 			            wr_clk_i,
	input wire 			            wr_en_i,
	input wire [DATA_WIDTH-1:0]		wr_data_i,

	input wire 			            rd_rst_b_i,
	input wire 			            rd_clk_i,
	input wire 			            rd_en_i,
	output reg [DATA_WIDTH-1:0]		rd_data_o,

	output reg 						full_o,
	output reg 						empty_o,

	input wire						debug_pull,
	input wire						debug_push,
);

	wire [ADDR_WIDTH-1:0] rd_addr_mem;
	wire [ADDR_WIDTH-1:0] wr_addr_mem;
	
	
	reg [ADDR_WIDTH-1:0]	wr_addr;
	wire [ADDR_WIDTH-1:0]	wr_addr_successor;
	reg [ADDR_WIDTH-1:0]	wr_addr_gray;
	reg [ADDR_WIDTH-1:0]	wr_addr_gray_rd;
	reg [ADDR_WIDTH-1:0]	wr_addr_gray_rd_r;
	reg [ADDR_WIDTH-1:0]	rd_addr;
	reg [ADDR_WIDTH-1:0]	rd_addr_gray;
	reg [ADDR_WIDTH-1:0]	rd_addr_gray_wr;
	reg [ADDR_WIDTH-1:0]	rd_addr_gray_wr_r;

	function [ADDR_WIDTH-1:0] gray_conv;
		input [ADDR_WIDTH-1:0] in;
		begin
			gray_conv = {in[ADDR_WIDTH-1], in[ADDR_WIDTH-2:0] ^ in[ADDR_WIDTH-1:1]};
		end
	endfunction

	
	/* note that the write gray is update every 2 */
	
	assign wr_addr_successor = wr_addr + 1'b1;
	always @(posedge wr_clk_i) begin
		if (wr_rst_b_i == 1'b0) begin
			wr_addr <= 0;
			wr_addr_gray <= 0;
		end else if (wr_en_i) begin
			wr_addr <= wr_addr_successor;
			wr_addr_gray <= gray_conv( {wr_addr_successor[ADDR_WIDTH-1:1],1'b0} ); // update every 2
		end
	end

	// synchronize read address to write clock domain
	always @(posedge wr_clk_i) begin
		rd_addr_gray_wr <= rd_addr_gray;
		rd_addr_gray_wr_r <= rd_addr_gray_wr;
	end

	always @(posedge wr_clk_i) begin
		if (wr_rst_b_i == 1'b0) begin
			full_o <= 0;
		end else if (wr_en_i) begin
			full_o <= gray_conv(wr_addr + 2) == rd_addr_gray_wr_r;
		end else begin
			full_o <= (gray_conv(wr_addr + 1'b1) == rd_addr_gray_wr_r);
		end
	end

	always @(posedge rd_clk_i) begin
		if (rd_rst_b_i == 1'b0) begin
			rd_addr <= 0;
			rd_addr_gray <= 0;
			
		end else if (rd_en_i) begin
			rd_addr <= rd_addr + 1'b1;
			rd_addr_gray <= gray_conv(rd_addr + 1'b1);
		end
	end

	// synchronize write address to read clock domain
	always @(posedge rd_clk_i) begin
		wr_addr_gray_rd <= wr_addr_gray;
		wr_addr_gray_rd_r <= wr_addr_gray_rd;
	end

	always @(posedge rd_clk_i) begin
		if (rd_rst_b_i == 1'b0) begin
			empty_o <= 1'b1;
		end else if (rd_en_i) begin
			empty_o <= gray_conv(rd_addr + 1) == wr_addr_gray_rd_r;
		end else begin
			empty_o <= (gray_conv(rd_addr) == wr_addr_gray_rd_r);
		end
	end

	/* swap I and Q samples */
	
	assign rd_addr_mem = (rd_en_i)? rd_addr +1 : rd_addr;
	assign wr_addr_mem = wr_addr ^ 1'b1;
	
	
	// ////
	// MEMORY OPERATIONS
	// ////
	always @(posedge rd_clk_i) begin
		rd_data_o <= mem_iq[rd_addr_mem];
		
	end
		
	always @(posedge wr_clk_i) begin
		if (wr_en_i) begin
				mem_iq[wr_addr_mem] <= wr_data_i;
		end
	end

	reg [(DATA_WIDTH)-1:0] mem_iq[(1<<(ADDR_WIDTH))-1:0];

endmodule
