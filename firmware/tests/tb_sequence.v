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
integer g,n,received,last,errors,zeros,burst,total_errors; reg [31:0] word;
initial begin
total_errors=0;
for(g=0;g<=15;g=g+1) begin
 limit=20;
 gap=g; rst=0;tx=0;repeat(4) @(negedge clk);rst=1;tx=1;
 received=0;last=-1;errors=0;zeros=0;word=0;
 for(burst=0;burst<3;burst=burst+1) begin
 limit=20*(burst+1);last=-1;zeros=0;
 for(n=0;n<8000;n=n+1) begin
 @(negedge clk);word={word[29:0],data};
 if(dut.r_phase_count==15) begin
 if(word[31:24]==8'h81) begin
 if(word!==sample(received)) begin errors=errors+1;$display("ORDER gap=%0d index=%0d got=%h expected=%h",g,received,word,sample(received));end
 if(last>=0 && ((n-last)/16!=g+1 || zeros!=g)) begin errors=errors+1;$display("GAP mismatch gap=%0d slots=%0d zeros=%0d",g,(n-last)/16,zeros);end
 received=received+1;last=n;zeros=0;
 end else if(word==0) zeros=zeros+1;
 else if(last>=0 && received<limit) begin errors=errors+1;$display("Unexpected intermediate word %h",word);end
 end
 end
 if(received!=limit) errors=errors+1;
 total_errors=total_errors+errors;
 $display("RESULT gap=%0d supplied=%0d received=%0d errors=%0d",g,supplied,received,errors);
 if(burst==1) begin
 tx=0;repeat(512) @(negedge clk);
 if(pull || dut.pending_load) $fatal(1,"stop did not settle");
 tx=1;
 end
 end
 tx=0;repeat(64) @(negedge clk);
end
if(total_errors) $fatal(1,"sequence errors=%0d",total_errors);
$display("PASS all gaps: initial burst, empty/refill, stop/restart");
$finish;
end
endmodule
