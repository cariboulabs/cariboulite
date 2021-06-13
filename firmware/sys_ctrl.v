module sys_ctrl
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
        ioc_module_version  = 5'b00000,     // read only
        ioc_system_version  = 5'b00001,     // read only
        ioc_manu_id         = 5'b00010;     // read only

    // MODULE SPECIFIC PARAMS
    // ----------------------
    localparam
        module_version  = 8'b00000001,
        system_version  = 8'b00000001,
        manu_id         = 8'b00000001;

    always @(posedge i_sys_clk)
    begin
        if (i_cs == 1'b1) begin
            if (i_fetch_cmd == 1'b1) begin
                case (i_ioc)
                    ioc_module_version: o_data_out <= module_version;
                    ioc_system_version: o_data_out <= system_version;
                    ioc_manu_id: o_data_out <= manu_id;
                endcase
            end
        end
    end

endmodule // sys_ctrl