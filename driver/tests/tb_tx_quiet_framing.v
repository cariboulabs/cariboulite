`timescale 1ns/1ps
module tb;
reg clk=0, rst=0, swe=1;
always #8 clk=~clk;
reg [7:0] data=0;
wire push;
wire [31:0] iq;
integer pushes=0, n, raw_zero=0;
reg [31:0] quiet_word;
smi_ctrl dut(
    .i_rst_b(rst), .i_sys_clk(clk), .i_ioc(5'd0), .i_data_in(8'd0),
    .i_cs(1'b0), .i_fetch_cmd(1'b0), .i_load_cmd(1'b0),
    .i_rx_fifo_pulled_data(32'd0), .i_rx_fifo_empty(1'b1),
    .o_tx_fifo_push(push), .o_tx_fifo_pushed_data(iq), .i_tx_fifo_full(1'b0),
    .i_smi_soe_se(1'b1), .i_smi_swe_srw(swe), .i_smi_data_in(data)
);
always @(posedge clk) if(push) begin
    pushes=pushes+1;
    // The current firmware hardcodes TX_EN high. Validate signed zero IQ,
    // its I/Q markers, and expose the actual TX_EN behavior explicitly.
    // d_byte is sampled and consumed on the same edge in the existing RTL.
    // Its first push uses reset data; the following bytes are delayed by one.
    if(!raw_zero && pushes==1 && iq!==32'h00000000)
        $fatal(1,"unexpected startup fallback: %08x",iq);
    if(!raw_zero && pushes>1 && iq!==32'h80014000)
        $fatal(1,"quiet IQ payload mismatch: %08x",iq);
end
initial begin
    if(!$value$plusargs("quiet_word=%h",quiet_word)) $fatal(1,"quiet_word missing");
    if($value$plusargs("raw_zero=%d",raw_zero));
    #100 rst=1;
    // SMI packed words reach the collector with the protocol SOF byte first.
    // This bench models that peripheral serialization, not host memory order.
    for(n=0;n<2048;n=n+1) begin
        if(raw_zero) data=8'd0;
        else data=(quiet_word >> (24-8*(n%4))) & 8'hff;
        #64 swe=0; #64 swe=1; #64;
    end
    if(!raw_zero) begin
        // Flush the existing collector's one-byte pipeline so all 512 framed
        // samples can be checked, in addition to the initial reset fallback.
        data=8'd0;
        #64 swe=0; #64 swe=1; #64;
    end
    #200;
    if(pushes!==(raw_zero ? 2048:513))
        $fatal(1,"framing expansion: raw_zero=%0d pushes=%0d",raw_zero,pushes);
    $display("PASS: raw_zero=%0d fifo_words=%0d (quiet case: 512 valid zero-IQ + 1 existing startup fallback)",raw_zero,pushes);
    $finish;
end
endmodule
