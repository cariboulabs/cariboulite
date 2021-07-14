module smi_ctrl
    (
        input               i_reset,
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
        output reg [7:0]    o_smi_data_out,
        input [7:0]         i_smi_data_in,
        output              o_smi_read_req,
        output              o_smi_write_req,
        output              o_smi_writing );

    // MODULE SPECIFIC IOC LIST
    // ------------------------
    localparam
        ioc_module_version  = 5'b00000,     // read only
        ioc_fifo_status     = 5'b00001;     // read-only

    // MODULE SPECIFIC PARAMS
    // ----------------------
    localparam
        module_version  = 8'b00000001;

    // MODULE RX STATE MACHINE
    localparam
        state_rx_idle = 3'b000,
        state_rx_fetch_fifo = 3'b001,
        state_rx_byte_0 = 3'b010,
        state_rx_byte_1 = 3'b011,
        state_rx_byte_2 = 3'b100,
        state_rx_byte_3 = 3'b101;

    assign o_smi_writing = 1'b0;

    always @(posedge i_sys_clk)
    begin
        if (i_reset) begin
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
    assign o_smi_read_req = !i_fifo_09_empty || !i_fifo_24_empty;

    reg [31:0] rx_data_buf_09;
    reg [31:0] rx_data_buf_24;

    reg [2:0] rx09_smi_state;
    reg [2:0] rx24_smi_state;
    reg [2:0] r_soe;
    always @(posedge i_sys_clk) r_soe <= {r_soe[1:0], i_smi_soe_se};
    wire soe_falling_edge = (r_soe[2:1]==2'b10);  // detecting synchronous soe falling edge

    always @(posedge i_sys_clk)
    begin
        if (i_reset) begin
            rx09_smi_state <= state_rx_idle;
            rx24_smi_state <= state_rx_idle;
        end else if (soe_falling_edge) begin
            //==========================
            //  0.9 GHz complex fifo
            //==========================
            if (i_smi_a == 3'b000) begin
                case (rx09_smi_state)
                    //----------------------------------------------
                    state_rx_idle: begin end

                    //----------------------------------------------
                    state_rx_fetch_fifo:  begin end

                    //----------------------------------------------
                    state_rx_byte_0: begin end

                    //----------------------------------------------
                    state_rx_byte_1: begin end

                    //----------------------------------------------
                    state_rx_byte_2: begin end
                    
                    //----------------------------------------------
                    state_rx_byte_3: begin end
                endcase
            end 
            //==========================
            //  2.4 GHz complex fifo
            //==========================
            else if (i_smi_a == 3'b001) begin
                case (rx24_smi_state)
                    //----------------------------------------------
                    state_rx_idle: begin end

                    //----------------------------------------------
                    state_rx_fetch_fifo:  begin end

                    //----------------------------------------------
                    state_rx_byte_0: begin end

                    //----------------------------------------------
                    state_rx_byte_1: begin end

                    //----------------------------------------------
                    state_rx_byte_2: begin end
                    
                    //----------------------------------------------
                    state_rx_byte_3: begin end
                endcase
            end 
            //==========================
            //  wrong address error
            //==========================
            else begin
              // error
            end
        end
    end

    assign o_smi_writing = i_smi_a[2];

endmodule // smi_ctrl