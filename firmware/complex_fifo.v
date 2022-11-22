module complex_fifo #(
    parameter ADDR_WIDTH = 10,
    parameter DATA_WIDTH = 16
)
(
    input wire 			            wr_rst_b_i,
    input wire 			            wr_clk_i,
    input wire 			            wr_en_i,
    input wire [2*DATA_WIDTH-1:0]	wr_data_i,

    input wire 			            rd_rst_b_i,
    input wire 			            rd_clk_i,
    input wire 			            rd_en_i,
    output reg [2*DATA_WIDTH-1:0]	rd_data_o,

    output reg 						full_o,
    output reg 						empty_o,

	input wire						debug_pull,
	input wire						debug_push,
);

reg [ADDR_WIDTH-1:0]	wr_addr;
reg [ADDR_WIDTH-1:0]	wr_addr_gray;
reg [ADDR_WIDTH-1:0]	wr_addr_gray_rd;
reg [ADDR_WIDTH-1:0]	wr_addr_gray_rd_r;
reg [ADDR_WIDTH-1:0]	rd_addr;
reg [ADDR_WIDTH-1:0]	rd_addr_gray;
reg [ADDR_WIDTH-1:0]	rd_addr_gray_wr;
reg [ADDR_WIDTH-1:0]	rd_addr_gray_wr_r;

reg [2*DATA_WIDTH-1:0] 	debug_buffer;

function [ADDR_WIDTH-1:0] gray_conv;
input [ADDR_WIDTH-1:0] in;
begin
	gray_conv = {in[ADDR_WIDTH-1],
		     in[ADDR_WIDTH-2:0] ^ in[ADDR_WIDTH-1:1]};
end
endfunction

always @(posedge wr_clk_i) begin
	if (wr_rst_b_i == 1'b0) begin
		wr_addr <= 0;
		wr_addr_gray <= 0;
	end else if (wr_en_i) begin
		wr_addr <= wr_addr + 1'b1;
		wr_addr_gray <= gray_conv(wr_addr + 1'b1);
	end
end

// synchronize read address to write clock domain
always @(posedge wr_clk_i) begin
	rd_addr_gray_wr <= rd_addr_gray;
	rd_addr_gray_wr_r <= rd_addr_gray_wr;
end

always @(posedge wr_clk_i)
	if (wr_rst_b_i == 1'b0)
		full_o <= 0;
	else if (wr_en_i)
		full_o <= gray_conv(wr_addr + 2) == rd_addr_gray_wr_r;
	else
		full_o <= full_o & (gray_conv(wr_addr + 1'b1) == rd_addr_gray_wr_r);

always @(posedge rd_clk_i) begin
	if (rd_rst_b_i == 1'b0) begin
		rd_addr <= 0;
		rd_addr_gray <= 0;
		debug_buffer <= 32'hABCDEF01;
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

always @(posedge rd_clk_i)
	if (rd_rst_b_i == 1'b0)
		empty_o <= 1'b1;
	else if (rd_en_i)
		empty_o <= gray_conv(rd_addr + 1) == wr_addr_gray_rd_r;
	else
		empty_o <= empty_o & (gray_conv(rd_addr) == wr_addr_gray_rd_r);

reg [DATA_WIDTH-1:0] mem[(1<<ADDR_WIDTH)-1:0];

always @(posedge rd_clk_i) begin
	if (rd_en_i) begin
		if (debug_pull) begin
			rd_data_o <= debug_buffer;
		end else begin
			rd_data_o[15:0] <= mem_q[rd_addr][15:0];
			rd_data_o[31:16] <= mem_i[rd_addr][15:0];
		end
	end
end

always @(posedge wr_clk_i) begin
	if (wr_en_i) begin
		if (debug_push) begin
			mem_q[wr_addr] <= debug_buffer[15:0];
			mem_i[wr_addr] <= debug_buffer[31:16];
		end else begin
			mem_q[wr_addr] <= wr_data_i[15:0];
			mem_i[wr_addr] <= wr_data_i[31:16];
		end
	end
end

reg [DATA_WIDTH-1:0] mem_i[(1<<ADDR_WIDTH)-1:0];
reg [DATA_WIDTH-1:0] mem_q[(1<<ADDR_WIDTH)-1:0];

endmodule






