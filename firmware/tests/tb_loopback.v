`timescale 1ns/1ps
module tb;
reg clk=0,sys=0; always #5 clk=~clk; always #7 sys=~sys;
reg rst=1,tx=0,empty=0,cs=0,load=0,fetch=0;
reg [4:0] addr=0; reg [7:0] din=0;
wire debug; wire [3:0] gap; wire [7:0] dout;
wire [1:0] data; wire pull; wire [2:0] state;
reg [31:0] fifo_data=32'h81004100;
integer pulls=0;
sys_ctrl ctrl(.i_rst_b(rst),.i_sys_clk(sys),.i_ioc(addr),.i_data_in(din),
 .o_data_out(dout),.i_cs(cs),.i_fetch_cmd(fetch),.i_load_cmd(load),
 .o_debug_loopback_tx(debug),.o_tx_sample_gap(gap));
lvds_tx dut(.i_rst_b(rst),.i_ddr_clk(clk),.i_fifo_empty(empty),
 .i_fifo_data(fifo_data),.i_sample_gap(gap),.i_tx_state(tx),.i_debug_lb(debug),
 .o_ddr_data(data),.o_fifo_pull(pull),.o_tx_fsm_state(state),.o_fifo_read_clk());
always @(posedge clk) begin
 if(!rst) begin pulls<=0;fifo_data<=32'h81004100;end
 else if(pull) begin
  if(empty) $fatal(1,"pull from empty FIFO");
  pulls<=pulls+1;fifo_data<=fifo_data+32'h00020002;
 end
end

task write_reg(input [4:0] a,input [7:0] value,input selected);
begin
 @(negedge sys);addr=a;din=value;cs=selected;load=1;fetch=0;
 @(negedge sys);cs=0;load=0;
end endtask

task reset;
begin
 rst=0; #1;
 if(debug!==0 || gap!==0 || state!==0 || pull!==0 || data!==0)
  $fatal(1,"reset outputs undefined or nonzero");
 repeat(4) @(negedge clk);rst=1;
end endtask

// Decode the actual two-bit serializer output, MSB pair first.
task frames(input integer mode);
integer n,seen; reg [31:0] word;
begin
 while(dut.r_phase_count!==15) @(negedge clk);
 word=0;seen=0;
 for(n=0;n<16*24;n=n+1) begin
  @(negedge clk);word={word[29:0],data};
  if(dut.r_phase_count==15) begin
   seen=seen+1;
   if(mode==1 && word!==32'h84037048)
    $fatal(1,"loopback word %h",word);
   if(mode==0 && word!==32'h00000000 && word!==32'h80004000)
    $fatal(1,"non-idle word after exit %h",word);
   if(mode==2 && word===32'h84037048)
    $fatal(1,"loopback word after normal TX resumes");
   if(^word===1'bx) $fatal(1,"unknown serializer output");
  end
 end
 if(seen!=24) $fatal(1,"decoder alignment");
end endtask

integer g,p,t,e,before_count,cases=0,bitnum;
initial begin
 reset;
 // Wrong address, missing chip select, and unrelated debug bits must not enable it.
 write_reg(5,8'h08,0);if(debug!==0) $fatal(1,"CS gating");
 write_reg(4,8'h08,1);if(debug!==0) $fatal(1,"address decoding");
 for(bitnum=0;bitnum<8;bitnum=bitnum+1) begin
  write_reg(5,8'b1<<bitnum,1);
  if(debug!==(bitnum==3)) $fatal(1,"debug bit decoding %0d",bitnum);
 end
 write_reg(5,0,1);
 for(g=0;g<16;g=g+1)
 for(p=0;p<16;p=p+1)
 for(t=0;t<2;t=t+1)
 for(e=0;e<2;e=e+1) begin
  tx=t;empty=e;reset;
  write_reg(6,g,1);
  if(gap!==g[3:0] || debug!==0) $fatal(1,"gap write affects debug");
  repeat(512) @(negedge clk);
  if(t && !e && pulls==0) $fatal(1,"normal TX setup failed");
  // Vary asynchronous register-write launch across all serializer phases.
  while(dut.r_phase_count!=p) @(negedge clk);
  write_reg(5,8'h08,1);
  if(debug!==1 || gap!==g[3:0]) $fatal(1,"register connection");
  repeat(512) @(negedge clk);
  if(state!==3 || pull!==0) $fatal(1,"loopback entry g=%0d p=%0d",g,p);
  before_count=pulls;frames(1);
  if(pulls!=before_count) $fatal(1,"FIFO consumed in loopback");
  // TX enable changes must not cancel an explicitly selected debug mode.
  tx=!t;repeat(512) @(negedge clk);frames(1);
  if(pulls!=before_count) $fatal(1,"TX toggle consumes FIFO in loopback");
  tx=t;
  write_reg(5,0,1);
  if(debug!==0) $fatal(1,"debug clear");
  repeat(512) @(negedge clk);
  if(t && !e) begin
   before_count=pulls;frames(2);
   if(pulls<=before_count) $fatal(1,"TX did not resume");
  end else begin
   if(state!==0 || pull!==0) $fatal(1,"idle exit");
   before_count=pulls;frames(0);
   if(pulls!=before_count) $fatal(1,"idle exit consumed FIFO");
  end
  // Reset while loopback is active must clear the register and transmitter.
  write_reg(5,8'h08,1);repeat(512) @(negedge clk);
  if(state!==3) $fatal(1,"re-entry");
  tx=0;reset;repeat(512) @(negedge clk);frames(0);
  cases=cases+1;
 end
 $display("PASS: register decode, %0d gap/phase/TX/FIFO cases, exact loopback frames, no FIFO pulls, exit/restart and reset",cases);
 $finish;
end
initial begin #100000000; $fatal(1,"timeout");end
endmodule
