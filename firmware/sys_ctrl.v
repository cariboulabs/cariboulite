module sys_ctrl
    (
        input               i_rst_b,
        input               i_sys_clk,        // FPGA Clock

        input [4:0]         i_ioc,
        input [7:0]         i_data_in,
        output reg [7:0]    o_data_out,
        input               i_cs,
        input               i_fetch_cmd,
        input               i_load_cmd,

        input [7:0]         i_error_list,

        // controls output
        output              o_debug_fifo_push,
        output              o_debug_fifo_pull,
        output              o_debug_smi_test,
    );

    // MODULE SPECIFIC IOC LIST
    // ------------------------
    localparam
        ioc_module_version  = 5'b00000,     // read only
        ioc_system_version  = 5'b00001,     // read only
        ioc_manu_id         = 5'b00010,     // read only
        ioc_error_state     = 5'b00011,     // read only
        ioc_debug_modes     = 5'b00101;     // write only

    // MODULE SPECIFIC PARAMS
    // ----------------------
    localparam
        module_version  = 8'b00000001,
        system_version  = 8'b00000001,
        manu_id         = 8'b00000001;

    // MODULE INTERNAL SIGNALS
    // -----------------------
    reg [3:0] reset_count;
    reg reset_cmd;
	reg debug_fifo_push;
	reg debug_fifo_pull;
	reg debug_smi_test;

	assign o_debug_fifo_push = debug_fifo_push;
	assign o_debug_fifo_pull = debug_fifo_pull;
	assign o_debug_smi_test = debug_smi_test;

    // MODULE MAIN PROCESS
    // -------------------
    always @(posedge i_sys_clk)
    begin
        if (i_rst_b == 1'b0) begin
            o_data_out <= 8'b00000000;
            debug_fifo_push <= 1'b0;
            debug_fifo_pull <= 1'b0;
            debug_smi_test <= 1'b0;
        end else if (i_cs == 1'b1) begin
            //=============================================
            // READ OPERATIONS
            //=============================================
            if (i_fetch_cmd == 1'b1) begin
                case (i_ioc)
                    ioc_module_version: o_data_out <= module_version;
                    ioc_system_version: o_data_out <= system_version;
                    ioc_manu_id: o_data_out <= manu_id;
                    ioc_error_state: o_data_out <= i_error_list;
                endcase
            end
            //=============================================
            // WRITE OPERATIONS
            //=============================================
            else if (i_load_cmd == 1'b1) begin
                case (i_ioc)
					//----------------------------------------------
					ioc_debug_modes: begin
						debug_fifo_push <= i_data_in[0];
						debug_fifo_pull <= i_data_in[1];
						debug_smi_test <= i_data_in[2];
					end
                endcase
            end
        end else begin
            reset_cmd <= 1'b0;
        end
    end

endmodule // sys_ctrl