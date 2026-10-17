`timescale 1ns/1ps
module tb;
reg glob=0, lvds=0, rst=1, divider=0, push=0;
always #4 glob=~glob;
always #7 lvds=~lvds;
// Exact system-clock reset behavior from top.v.
always @(posedge glob) divider<=!divider;
wire [31:0] data;
wire full,empty;
complex_fifo #(.ADDR_WIDTH(4)) dut(
.wr_rst_b_i(rst),.wr_clk_i(divider),.wr_en_i(push),.wr_data_i(32'h12345678),
.rd_rst_b_i(rst),.rd_clk_i(lvds),.rd_en_i(1'b0),.rd_data_o(data),.full_o(full),.empty_o(empty));
initial begin
 repeat(4) @(negedge divider);
 push=1;
 repeat(4) @(negedge divider);
 push=0;
 #1;
 if(dut.wr_addr!=4) $fatal(1,"setup failed");
 rst=0;
 #200;
 $display("During reset: wr_addr=%d rd_addr=%d",dut.wr_addr,dut.rd_addr);
 if(dut.wr_addr!=0 || dut.rd_addr!=0) $fatal(1,"unexpected reset behavior");
 rst=1;
 repeat(8) @(negedge divider);
 $display("After reset: wr_addr=%d empty=%b",dut.wr_addr,empty);
 if(dut.wr_addr!=0 || empty!=1) $fatal(1,"FIFO not empty after reset");
 $display("PASS: running system clock resets FIFO pointers and clears stale availability");
 $finish;
end
initial begin #10000; $fatal(1,"timeout"); end
endmodule
