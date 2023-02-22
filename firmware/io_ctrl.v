module io_ctrl
    (
        input               i_rst_b,
	input               i_sys_clk,        // FPGA Clock

	input [4:0]         i_ioc,
	input [7:0]         i_data_in,
	output reg [7:0]    o_data_out,
	input               i_cs,
	input               i_fetch_cmd,
	input               i_load_cmd,

	// Digital interfaces
	input               i_button,
	input [3:0]         i_config,
	output              o_led0,
	output              o_led1,
	output [7:0]        o_pmod,

	// Analog interfaces
	output              o_mixer_fm,
	output              o_rx_h_tx_l,
	output              o_rx_h_tx_l_b,
	output              o_tr_vc1,
	output              o_tr_vc1_b,
	output              o_tr_vc2,
	output              o_shdn_tx_lna,
	output              o_shdn_rx_lna,
	output              o_mixer_en 
    );
				

    //=========================================================================
    // CONSTANT DEFINITIONS
    //=========================================================================

    // MODULE SPECIFIC IOC LIST
    localparam
        ioc_module_version  = 5'b00000,     // read only
        ioc_mode            = 5'b00001,     // The functional mode-of-operation of the system.
        ioc_dig_pin         = 5'b00010,     // Digital pin control and read
        ioc_pmod_dir        = 5'b00011,     // PMOD connector bits IO pin direction
        ioc_pmod_val        = 5'b00100,     // PMOD connector bits IO pin value
        ioc_rf_pin          = 5'b00101;     // Setting up / reading out the pin values controlling the RF front-end path switches.

    // MODULE SPECIFIC PARAMS
    localparam
        module_version  = 8'b00000001;

    // Debug state definitions
    localparam
        debug_mode_none = 2'b00,    // No-debug mode - the RFM field describes a set of pre-determines modes of operation
        debug_mode_debug = 2'b01;   // Debug mode - the RFM field directives are ignored and the RF-I/O settings are explicitly set in pin-level

    // RF-mode states
    localparam                      // RF Mode of operation - this setting is active only when DBG='00'
        rf_mode_low_power = 3'b000, // Low-power / inactive mode - all RF peripherals are turned off (LNAs, Mixer, etc.)
        rf_mode_bypass    = 3'b001, // RF front-end wide-range tuning is turned off, and the modem 2.4GHz
        rf_mode_rx_lpf    = 3'b010, // RX Lowpass mode - tuned to receive high-frequency signals (>2.483 GHz)
        rf_mode_rx_hpf    = 3'b011, // RX Highpass mode - tuned to receive low-frequency signals (<2.4 GHz)
        rf_mode_tx_lpf    = 3'b100, // TX Lowpass mode - tuned to transmit low-frequency signals (<2.4 GHz)
        rf_mode_tx_hpf    = 3'b101; // TX Highpass mode - tuned to transmit high-frequency signals (>2.4 GHz)


    //=========================================================================
    // INTERNAL REGISTERS
    //=========================================================================
    // Internal registers
    reg [1:0]   debug_mode;
    reg [2:0]   rf_mode;

    // Digital outputs
    reg         led0_state;
    reg         led1_state;
    reg [7:0]   pmod_dir_state;
    reg [7:0]   pmod_state;
    reg [7:0]   rf_pin_state;
    reg         mixer_en_state;

    // Analog parts outputs
    reg         lna_rx_shutdown_state;
    reg         lna_tx_shutdown_state;
    reg         rx_h_state;
    reg         rx_h_b_state;
    reg         tr_vc_1_state;
    reg         tr_vc_1_b_state;
    reg         tr_vc_2_state;

    //=========================================================================
    // LOGICAL SIGNAL ASSIGNMENTS
    //=========================================================================
    assign o_led0 = led0_state;
    assign o_led1 = led1_state;
    assign o_pmod = pmod_state;

    // Analog interfaces
    assign o_mixer_fm = 1'b0;
    assign o_rx_h_tx_l = rx_h_state;
    assign o_rx_h_tx_l_b = rx_h_b_state;
    assign o_tr_vc1 = tr_vc_1_state;
    assign o_tr_vc1_b = tr_vc_1_b_state;
    assign o_tr_vc2 = tr_vc_2_state;
    assign o_shdn_tx_lna = lna_tx_shutdown_state;
    assign o_shdn_rx_lna = lna_rx_shutdown_state;
    //assign o_mixer_en = mixer_en_state;
    assign o_mixer_en = 1'b1;

    //=========================================================================
    // BUS COMMUNICATION LOGIC & RF CONTROL
    //=========================================================================
    always @(posedge i_sys_clk or negedge i_rst_b)
    begin
        if (i_rst_b == 1'b0) begin
            debug_mode <= debug_mode_none;
            rf_mode <= rf_mode_low_power;
            led0_state <= 1'b0;
            led1_state <= 1'b0;
        end else begin
            if (i_cs == 1'b1) begin
                //=============================================
                // READ OPERATIONS
                //=============================================
                if (i_fetch_cmd == 1'b1) begin
                    case (i_ioc)
                        //----------------------------------------------
                        ioc_module_version: o_data_out <= module_version;

                        //----------------------------------------------
                        ioc_mode: begin
                            o_data_out[1:0] <= debug_mode;
                            o_data_out[4:2] <= rf_mode;
                        end

                        //----------------------------------------------
                        ioc_dig_pin: begin
                            o_data_out[0] <= led0_state;
                            o_data_out[1] <= led1_state;
                            o_data_out[6:3] <= i_config;
                            o_data_out[7] <= i_button;
                        end

                        //----------------------------------------------
                        ioc_pmod_dir: begin
                            o_data_out <= pmod_dir_state;
                        end

                        //----------------------------------------------
                        ioc_pmod_val: begin
                            o_data_out <= pmod_state;
                        end

                        //----------------------------------------------
                        ioc_rf_pin: begin
                            o_data_out[0] <= mixer_en_state;
                            o_data_out[1] <= lna_rx_shutdown_state;
                            o_data_out[2] <= lna_tx_shutdown_state;
                            o_data_out[3] <= tr_vc_2_state;
                            o_data_out[4] <= tr_vc_1_b_state;
                            o_data_out[5] <= tr_vc_1_state;
                            o_data_out[6] <= rx_h_b_state;
                            o_data_out[7] <= rx_h_state;
                        end
                    endcase
                end
                //=============================================
                // WRITE OPERATIONS
                //=============================================
                else if (i_load_cmd == 1'b1) begin
                    case (i_ioc)
                        //----------------------------------------------
                        ioc_mode: begin
                            debug_mode <= i_data_in[1:0];
                            rf_mode <= i_data_in[4:2];

                            if (i_data_in[1:0] == debug_mode_none) begin
                                // TBD move back all the controls to good position
                                // use rf_mode to control everything
                            end
                        end

                        //----------------------------------------------
                        ioc_dig_pin: begin
                            led0_state <= i_data_in[0];
                            led1_state <= i_data_in[1];
                        end

                        //----------------------------------------------
                        ioc_pmod_dir: begin
                            pmod_dir_state <= i_data_in;
                        end

                        //----------------------------------------------
                        ioc_pmod_val: begin
                            pmod_state <= i_data_in;
                        end

                        //----------------------------------------------
                        ioc_rf_pin: begin
                            rf_pin_state <= i_data_in;
                        end
                        //----------------------------------------------
                    endcase
                end
            end
        end
    end


    always @(posedge i_sys_clk or negedge i_rst_b)
    begin
    	if (i_rst_b == 1'b0) begin
            
        end
        // this is relevant only if the system runs
        // in an operational mode
        else if (debug_mode == debug_mode_none) begin
            case (rf_mode)
                //--------------------------------------------------
                rf_mode_low_power: begin
                    mixer_en_state <= 1'b0;
                    lna_rx_shutdown_state <= 1'b1;
                    lna_tx_shutdown_state <= 1'b1;

                    tr_vc_1_state <= 1'b0;
                    tr_vc_1_b_state <= 1'b1;
                    tr_vc_2_state <= 1'b0;
                    rx_h_state <= 1'b0;
                    rx_h_b_state <= 1'b1;
                end

                //--------------------------------------------------
                rf_mode_bypass: begin
                    mixer_en_state <= 1'b0;
                    lna_rx_shutdown_state <= 1'b1;
                    lna_tx_shutdown_state <= 1'b1;

                    tr_vc_1_state <= 1'b1;
                    tr_vc_1_b_state <= 1'b0;
                    tr_vc_2_state <= 1'b0;
                    rx_h_state <= 1'b0;
                    rx_h_b_state <= 1'b1;
                end

                //--------------------------------------------------
                rf_mode_rx_lpf: begin
                    mixer_en_state <= 1'b1;
                    lna_rx_shutdown_state <= 1'b0;
                    lna_tx_shutdown_state <= 1'b1;

                    tr_vc_1_state <= 1'b0;
                    tr_vc_1_b_state <= 1'b1;
                    tr_vc_2_state <= 1'b1;
                    rx_h_state <= 1'b1;
                    rx_h_b_state <= 1'b0;
                end

                //--------------------------------------------------
                rf_mode_rx_hpf: begin
                    mixer_en_state <= 1'b1;
                    lna_rx_shutdown_state <= 1'b0;
                    lna_tx_shutdown_state <= 1'b1;

                    tr_vc_1_state <= 1'b0;
                    tr_vc_1_b_state <= 1'b1;
                    tr_vc_2_state <= 1'b1;
                    rx_h_state <= 1'b0;
                    rx_h_b_state <= 1'b1;
                end

                //--------------------------------------------------
                rf_mode_tx_lpf: begin
                    mixer_en_state <= 1'b1;
                    lna_rx_shutdown_state <= 1'b1;
                    lna_tx_shutdown_state <= 1'b0;

                    tr_vc_1_state <= 1'b1;
                    tr_vc_1_b_state <= 1'b0;
                    tr_vc_2_state <= 1'b1;
                    rx_h_state <= 1'b0;
                    rx_h_b_state <= 1'b1;
                end

                //--------------------------------------------------
                rf_mode_tx_hpf: begin
                    mixer_en_state <= 1'b1;
                    lna_rx_shutdown_state <= 1'b1;
                    lna_tx_shutdown_state <= 1'b0;

                    tr_vc_1_state <= 1'b1;
                    tr_vc_1_b_state <= 1'b0;
                    tr_vc_2_state <= 1'b1;
                    rx_h_state <= 1'b1;
                    rx_h_b_state <= 1'b0;
                end
                //--------------------------------------------------
            endcase
        end else if (debug_mode == debug_mode_debug) begin
            mixer_en_state <= rf_pin_state[0];
            lna_rx_shutdown_state <= rf_pin_state[1];
            lna_tx_shutdown_state <= rf_pin_state[2];
            tr_vc_2_state <= rf_pin_state[3];
            tr_vc_1_b_state <= rf_pin_state[4];
            tr_vc_1_state <= rf_pin_state[5];
            rx_h_b_state <= rf_pin_state[6];
            rx_h_state <= rf_pin_state[7];
        end
    end

endmodule // io_ctrl