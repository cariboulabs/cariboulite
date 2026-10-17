module tb;
reg clk=0; always #5 clk=~clk;
reg rst=1,tx=0; reg [3:0] gap=0;
reg [31:0] fifo_data=0; integer supplied=0,limit=20;
wire empty=(supplied>=limit); wire [1:0] data; wire pull;
lvds_tx dut(.i_rst_b(rst),.i_ddr_clk(clk),.i_fifo_empty(empty),
.i_fifo_data(fifo_data),.i_sample_gap(gap),.i_tx_state(tx),.i_debug_lb(1'b0),
.o_ddr_data(data),.o_fifo_pull(pull),.o_tx_fsm_state(),.o_fifo_read_clk());
function [31:0] sample; input integer n; begin sample=32'h81004100+(n<<17)+(n<<1);end endfunction
always @(posedge clk) begin
 if(!rst) begin supplied<=0;fifo_data<=0;end
 else if(pull) begin
 if(empty) $fatal(1,"pull from empty FIFO");
 fifo_data<=sample(supplied); supplied<=supplied+1;
 end
end
integer g,phase,k,previous_count;
initial begin
limit=100000;
for(g=0;g<=15;g=g+1) begin
 for(phase=0;phase<16;phase=phase+1) begin
 gap=g;rst=0;tx=0;repeat(4) @(negedge clk);rst=1;tx=1;
 repeat(400+phase) @(negedge clk);
 tx=0;repeat(512) @(negedge clk);
 if(dut.r_state!==0 || dut.pending_load || pull) $fatal(1,"stop gap=%0d phase=%0d",g,phase);
 previous_count=supplied;
 repeat(32) @(negedge clk);
 if(supplied!=previous_count) $fatal(1,"pull while off");
 tx=1;repeat(512) @(negedge clk);
 if(supplied<=previous_count) $fatal(1,"restart failed");
 end
end
$display("PASS active stop/restart at all 16 phase offsets and all 16 gaps");
$finish;
end
endmodule
