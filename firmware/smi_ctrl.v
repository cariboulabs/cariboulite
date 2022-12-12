module smi_ctrl (	input               i_rst_b,
					input               i_sys_clk,        // FPGA Clock
					input 				i_fast_clk,

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
					output reg [7:0]    o_smi_data_out,
					input [7:0]         i_smi_data_in,
					output              o_smi_read_req,
					output              o_smi_write_req,
					output              o_smi_writing,
					input               i_smi_test,

					// Errors
					output reg          o_address_error );



    // MODULE SPECIFIC IOC LIST
    // ------------------------
    localparam
        ioc_module_version  = 5'b00000,     // read only
        ioc_fifo_status     = 5'b00001;     // read-only

    // MODULE SPECIFIC PARAMS
    // ----------------------
    localparam
        module_version  = 8'b00000001;

    always @(posedge i_sys_clk)
    begin
        if (i_rst_b == 1'b0) begin
            o_address_error <= 1'b0;
            // put the initial states here
        end else begin
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
            end
        end
    end

    // Tell the RPI that data is pending in either of the two fifos
    assign o_smi_read_req = !i_fifo_09_empty || !i_fifo_24_empty || i_smi_test;
    reg [4:0] int_cnt_09;
    reg [4:0] int_cnt_24;
    reg r_fifo_09_pull;
    reg r_fifo_09_pull_1;
    wire w_fifo_09_pull_trigger;
    reg r_fifo_24_pull;
    reg r_fifo_24_pull_1;
    wire w_fifo_24_pull_trigger;
    reg [7:0] r_smi_test_count_09;
    reg [7:0] r_smi_test_count_24;

    wire soe_and_reset;
    assign soe_and_reset = i_rst_b & i_smi_soe_se;

    always @(negedge soe_and_reset)
    begin
        if (i_rst_b == 1'b0) begin
            int_cnt_09 <= 5'd31;
            int_cnt_24 <= 5'd31;
            r_smi_test_count_09 <= 8'h56;
            r_smi_test_count_24 <= 8'h56;
        end else begin
            w_fifo_09_pull_trigger <= (int_cnt_09 == 5'd7) && !i_smi_test;
            w_fifo_24_pull_trigger <= (int_cnt_24 == 5'd7) && !i_smi_test;

            if (i_smi_a[2] == 1'b0) begin
                if ( i_smi_test ) begin
                    o_smi_data_out <= r_smi_test_count_09;
                    r_smi_test_count_09 <= {((r_smi_test_count_09[2] ^ r_smi_test_count_09[3]) & 1'b1), r_smi_test_count_09[7:1]};		    
                end else begin
                    int_cnt_09 <= int_cnt_09 - 8;
                    o_smi_data_out <= i_fifo_09_pulled_data[int_cnt_09:int_cnt_09-7];
                end

            end else if (i_smi_a[2] == 1'b1) begin
                if ( i_smi_test ) begin
                    o_smi_data_out <= r_smi_test_count_24;
                    r_smi_test_count_24 <= {((r_smi_test_count_24[2] ^ r_smi_test_count_24[3]) & 1'b1), r_smi_test_count_24[7:1]};
                end else begin
                    int_cnt_24 <= int_cnt_24 - 8;
                    o_smi_data_out <= i_fifo_24_pulled_data[int_cnt_24:int_cnt_24-7];
                end
            end

        end
    end

    always @(posedge i_fast_clk)
    begin
        if (i_rst_b == 1'b0) begin
            r_fifo_09_pull <= 1'b0;
            r_fifo_24_pull <= 1'b0;
            r_fifo_09_pull_1 <= 1'b0;
            r_fifo_24_pull_1 <= 1'b0;
	    r_fifo_09_pull_2 <= 1'b0;
	    r_fifo_24_pull_2 <= 1'b0;
        end else begin
            r_fifo_09_pull <= w_fifo_09_pull_trigger;
            r_fifo_24_pull <= w_fifo_24_pull_trigger;
            r_fifo_09_pull_1 <= r_fifo_09_pull;
            r_fifo_24_pull_1 <= r_fifo_24_pull;
	    r_fifo_09_pull_2 <= r_fifo_09_pull_1;
            r_fifo_24_pull_2 <= r_fifo_24_pull_1;
        end
    end

    assign o_fifo_09_pull = !r_fifo_09_pull_1 && r_fifo_09_pull && !i_fifo_09_empty;
    assign o_fifo_24_pull = !r_fifo_24_pull_1 && r_fifo_24_pull && !i_fifo_24_empty;
    
	// SMI Addressing description
	// ==========================
	// In CaribouLite, the SMI addresses are connected as follows:
	//
	//		RPI PIN			| 	FPGA TOP-LEVEL SIGNAL
	//		------------------------------------------------------------------------
	//		GPIO2_SA3		| 	i_smi_a[2] - RX09 / RX24 channel select
	//		GPIO3_SA2		| 	i_smi_a[1] - Tx SMI (0) / Rx SMI (1) select
	//		GPIO4_SA1		| 	i_smi_a[0] - used as a sys async reset (GBIN1)
	//		GPIO5_SA0		| 	Not connected to FPGA (MixerRst)
	//
	// In order to perform SMI data bus direction selection (highZ / PushPull)
	// signal a[0] was chosen, while the '0' level (default) denotes RPI => FPGA
	// direction, and the DATA bus is highZ (recessive mode).
	// The signal a[2] selects the RX source (900 MHZ or 2.4GHz)
	// The signal a[1] can be used in the future for other purposes
	//
	// Description	|	a[2]	|	   a[1]		|	
	// -------------|-----------|---------------|
	// 				|	  0		|	   0		|		
	// 		TX		|-----------| RPI => FPGA   |
	// 				|	  1		| Data HighZ	|		
	// -------------|-----------|---------------|
	// 		RX09	|	  0		|	   1		|	
	// -------------|-----------| FPGA => RPI	|	
	// 		RX24	|	  1		| Data PushPull	|		
	// -------------|-----------|---------------|
	assign o_smi_writing = i_smi_a[1];

endmodule // smi_ctrl