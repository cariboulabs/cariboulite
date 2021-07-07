/*
 * Copyright (c) 2012, Stefan Kristiansson <stefan.kristiansson@saunalahti.fi>
 * All rights reserved.
 *
 * Based on vga_fifo_dc.v in Richard Herveille's VGA/LCD core
 * Copyright (C) 2001 Richard Herveille <richard@asics.ws>
 *
 * Redistribution and use in source and non-source forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in non-source form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS WORK IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * WORK, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

module dual_clock_fifo #(
	parameter ADDR_WIDTH = 8,
	parameter DATA_WIDTH = 16
)
(
	input wire 			wr_rst_i,
	input wire 			wr_clk_i,
	input wire 			wr_en_i,
	input wire [DATA_WIDTH-1:0]	wr_data_i,

	input wire 			rd_rst_i,
	input wire 			rd_clk_i,
	input wire 			rd_en_i,
	output reg [DATA_WIDTH-1:0]	rd_data_o,

	output reg 			full_o,
	output reg 			empty_o
);

reg [ADDR_WIDTH-1:0]	wr_addr;
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
	gray_conv = {in[ADDR_WIDTH-1],
		     in[ADDR_WIDTH-2:0] ^ in[ADDR_WIDTH-1:1]};
end
endfunction

always @(posedge wr_clk_i) begin
	if (wr_rst_i) begin
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
	if (wr_rst_i)
		full_o <= 0;
	else if (wr_en_i)
		full_o <= gray_conv(wr_addr + 2) == rd_addr_gray_wr_r;
	else
		full_o <= full_o & (gray_conv(wr_addr + 1'b1) == rd_addr_gray_wr_r);

always @(posedge rd_clk_i) begin
	if (rd_rst_i) begin
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

always @(posedge rd_clk_i)
	if (rd_rst_i)
		empty_o <= 1'b1;
	else if (rd_en_i)
		empty_o <= gray_conv(rd_addr + 1) == wr_addr_gray_rd_r;
	else
		empty_o <= empty_o & (gray_conv(rd_addr) == wr_addr_gray_rd_r);

reg [DATA_WIDTH-1:0] mem[(1<<ADDR_WIDTH)-1:0];

always @(posedge rd_clk_i)
	if (rd_en_i)
		rd_data_o <= mem[rd_addr];

always @(posedge wr_clk_i)
	if (wr_en_i)
		mem[wr_addr] <= wr_data_i;
endmodule
