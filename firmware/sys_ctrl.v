module sys_ctrl
    (
        input               i_rst_b,          // FPGA Reset
        input               i_sys_clk,        // FPGA Clock

        input [4:0]         i_ioc,
        input [7:0]         i_data_in,
        output reg [7:0]    o_data_out,
        input               i_cs,
        input               i_fetch_cmd,
        input               i_load_cmd,

        output reg          o_soft_reset );

    // MODULE SPECIFIC IOC LIST
    // ------------------------
    localparam
        ioc_module_version  = 5'b00000,     // read only
        ioc_system_version  = 5'b00001,     // read only
        ioc_manu_id         = 5'b00010,     // read only
        ioc_error_state     = 5'b00011,     // read only
        ioc_soft_reset      = 5'b00100;     // write only

    // MODULE SPECIFIC PARAMS
    // ----------------------
    localparam
        module_version  = 8'b00000001,
        system_version  = 8'b00000001,
        manu_id         = 8'b00000001;

    reg [3:0] reset_count;

    always @(posedge i_sys_clk)
    begin
        if (i_cs == 1'b1) begin
            //=============================================
            // READ OPERATIONS
            //=============================================
            if (i_fetch_cmd == 1'b1) begin
                case (i_ioc)
                    ioc_module_version: o_data_out <= module_version;
                    ioc_system_version: o_data_out <= system_version;
                    ioc_manu_id: o_data_out <= manu_id;
                    ioc_error_state: o_data_out <= 8'b00000000;
                endcase
            end
            //=============================================
            // WRITE OPERATIONS
            //=============================================
            else if (i_load_cmd == 1'b1) begin
                //case (i_ioc)
                //    ioc_soft_reset: begin reset_count <= 3; end
                //endcase
            end
        end
    end


    // Reset state process
    always @(posedge i_sys_clk)
    begin
        if (reset_count < 4'd15) begin
            reset_count <= reset_count + 1'b1;
        end else if (reset_count == 4'd15) begin
            reset_count <= reset_count;
        end else begin
            reset_count <= 0; 
        end
    end

    always @(posedge i_sys_clk)
    begin
        if (reset_count == 4'd15)
            o_soft_reset <= 1'b0;
        else
            o_soft_reset <= 1'b1;
    end

endmodule // sys_ctrl