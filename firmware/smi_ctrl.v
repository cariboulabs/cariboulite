module smi_ctrl
    (
        input               i_rst_b,          // FPGA Reset
        input               i_sys_clk,        // FPGA Clock

        input [4:0]         i_ioc,
        input [7:0]         i_data_in,
        output reg [7:0]    o_data_out,
        input               i_cs,
        input               i_fetch_cmd,
        input               i_load_cmd,
        
        // FIFO INTERFACE 0.9 GHz
        output              o_fifo_09_pull,
        input [31:0]        i_fifo_09_pulled_data,
        input               i_fifo_09_full,
        input               i_fifo_09_empty,

        // FIFO INTERFACE 2.4 GHz
        output              o_fifo_24_pull,
        input [31:0]        i_fifo_24_pulled_data,
        input               i_fifo_24_full,
        input               i_fifo_24_empty,

        // SMI INTERFACE
        input [2:0]         i_smi_a,
        input               i_smi_soe_se,
        input               i_smi_swe_srw,
        output [7:0]        o_smi_data_out,
        inout [7:0]         i_smi_data_in,
        output              o_smi_read_req,
        output              o_smi_write_req,
        output              o_smi_writing  );

    // MODULE SPECIFIC IOC LIST
    // ------------------------
    localparam
        ioc_module_version  = 5'b00000,     // read only
        ioc_fifo_status     = 5'b00001;     // read-only

    // MODULE SPECIFIC PARAMS
    // ----------------------
    localparam
        module_version  = 8'b00000001;

    assign o_smi_writing = 1'b0;

    always @(posedge i_sys_clk)
    begin
        if (i_cs == 1'b1) begin
            if (i_fetch_cmd == 1'b1) begin
                case (i_ioc)
                    //----------------------------------------------
                    ioc_module_version: o_data_out <= module_version; // Module Version

                    //----------------------------------------------
                    ioc_fifo_status: begin
                        o_data_out[0] <= i_fifo_09_empty;
                        o_data_out[1] <= i_fifo_09_full;
                        o_data_out[2] <= i_fifo_24_empty;
                        o_data_out[3] <= i_fifo_24_full;
                        o_data_out[7:4] <= 4'b0000;
                    end

                endcase
            end
        end else begin
            o_data_out <= 8'b00000000;
        end
    end

    always @(posedge i_sys_clk)
    begin
        if (i_fifo_09_empty == 1'b0) begin
          
        end
    end


endmodule // smi_ctrl