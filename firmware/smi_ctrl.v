module smi_ctrl
    (
        input               i_rst_b,          // FPGA Reset
        input               i_sys_clk,        // FPGA Clock

        input [4:0]         i_ioc,
        input [7:0]         i_data_in,
        output reg [7:0]    o_data_out,
        input               i_cs,
        input               i_fetch_cmd,
        input               i_load_cmd );

    // MODULE SPECIFIC IOC LIST
    // ------------------------
    localparam
        ioc_module_version  = 5'b00000;     // read only

    // MODULE SPECIFIC PARAMS
    // ----------------------
    localparam
        module_version  = 8'b00000001;

    always @(posedge i_sys_clk)
    begin
        if (i_cs == 1'b1) begin
            if (i_fetch_cmd == 1'b1) begin
                case (i_ioc)
                    ioc_module_version: o_data_out <= module_version; // Module Version
                endcase
            end
        end else begin
            o_data_out <= 8'b00000000;
        end
    end

endmodule // smi_ctrl