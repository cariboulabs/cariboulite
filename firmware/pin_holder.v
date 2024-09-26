module pin_holder (
    input        i_rst_b,
    input        i_sys_clk,

    input             i_data_in,
    output            o_data_out,

);

    wire pin_sync;
    reg [3:0] pin_shift;
    reg [31:0] pin_counter;
    wire pin_deb;
    
always @(posedge i_sys_clk or negedge i_rst_b) begin
        if (i_rst_b == 1'b0) begin
            pin_shift <= 4'd0;
            pin_counter <= 32'h0;
        end else begin
            pin_shift <= {i_data_in, pin_shift[3:1]};
            if (pin_sync ==1'b1 && pin_deb == 1'b0) begin
                pin_counter <= pin_counter +1;
            end
            
        end
end

assign pin_sync = pin_shift[0];
assign pin_deb = pin_counter[26];
assign o_data_out  = pin_sync & i_data_in; //pin deb

/*
pin_holder pin_holder_ins (
      .i_rst_b(i_rst_b),
      .i_sys_clk(w_clock_sys),
      .i_data_in(i_smi_a2),
      .o_data_out(w_smi_data_direction),
      );
      */

      
endmodule 
