`include "spi_if.v"
`include "sys_ctrl.v"
`include "io_ctrl.v"
`include "smi_ctrl.v"
`include "lvds_rx.v"
`include "lvds_tx.v"
`include "complex_fifo.v"

module top (
    input i_glob_clock,
    input i_rst_b,

    // RF FRONT-END PATH
    output o_rx_h_tx_l,
    output o_rx_h_tx_l_b,
    output o_tr_vc1,
    output o_tr_vc1_b,
    output o_tr_vc2,
    output o_shdn_rx_lna,
    output o_shdn_tx_lna,

    // MODEM (LVDS & CLOCK)
    output o_iq_tx_p,
    output o_iq_tx_n,
    output o_iq_tx_clk_p,
    output o_iq_tx_clk_n,
    input  i_iq_rx_09_p,   // Paired with i_iq_rx_09_n - only the 'B' pins need to be specified
    input  i_iq_rx_24_n,   // Paired with i_iq_rx_24_p - only the 'B' pins need to be specified
    input  i_iq_rx_clk_p,  // Paired with i_iq_rx_clk_n - only the 'B' pins need to be specified

    // Note: The icestorm (specifically nextpnr) fails to build if both diff pins are constrained
    //       in the constrain file and the interface herein. Thus we need to take them out so that
    //       it will "understand" we actually want an LVDS pair inputs. In addition, the pair is
    //       defined only by the "B" pins in BANK3 and not the "A" pins (which is counter-logical)

    // MIXER
    output o_mixer_fm,
    output o_mixer_en,

    // DIGITAL I/F
    input [3:0] i_config,
    input i_button,
    inout [7:0] io_pmod,
    output o_led0,
    output o_led1,

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
    // Description	| a[2] (SA3)|   a[1] (SA2)   |	
    // -------------|------------|---------------|
    // 				|	  0		 |	   0		 |		
    // 		TX		|------------| RPI => FPGA   |
    // 				|	  1		 | Data HighZ	 |		
    // -------------|------------|---------------|
    // 		RX09	|	  0		 |	   1		 |	
    // -------------|------------| FPGA => RPI	 |	
    // 		RX24	|	  1		 | Data PushPull |		
    // -------------|------------|---------------|	
    input i_smi_a2,
    input i_smi_a3,

    input i_smi_soe_se,
    input i_smi_swe_srw,
    inout [7:0] io_smi_data,
    output o_smi_write_req,
    output o_smi_read_req,

    // SPI
    input  i_mosi,
    input  i_sck,
    input  i_ss,
    output o_miso
);


  //=========================================================================
  // INNER SIGNALS
  //=========================================================================
  reg        r_counter;
  wire       w_clock_spi;
  wire       w_clock_sys;
  wire [4:0] w_ioc;
  wire [7:0] w_rx_data;
  reg  [7:0] r_tx_data;
  wire [3:0] w_cs;
  wire       w_fetch;
  wire       w_load;

  wire [7:0] w_tx_data_sys;
  wire [7:0] w_tx_data_io;
  wire [7:0] w_tx_data_smi;

  //=========================================================================
  // INSTANCES
  //=========================================================================
  spi_if spi_if_ins (
      .i_rst_b(i_rst_b),
      .i_sys_clk(w_clock_sys),
      .o_ioc(w_ioc),
      .o_data_in(w_rx_data),
      .i_data_out(r_tx_data),
      .o_cs(w_cs),
      .o_fetch_cmd(w_fetch),
      .o_load_cmd(w_load),

      // SPI Interface
      .i_spi_sck (i_sck),
      .o_spi_miso(int_miso),
      .i_spi_mosi(i_mosi),
      .i_spi_cs_b(i_ss)
  );

  wire int_miso;
  SB_IO #(
      .PIN_TYPE(6'b1010_01),
      .PULLUP  (1'b0)
  ) miso_out (
      .PACKAGE_PIN(o_miso),
      .OUTPUT_ENABLE(!i_ss),
      .D_OUT_0(int_miso),
      .D_IN_0()
  );


  //assign o_miso = (i_ss) ? 1'bZ : int_miso;

  // SYSTEM CTRL
  sys_ctrl sys_ctrl_ins (
      .i_rst_b(i_rst_b),
      .i_sys_clk(w_clock_sys),
      .i_ioc(w_ioc),
      .i_data_in(w_rx_data),
      .o_data_out(w_tx_data_sys),
      .i_cs(w_cs[0]),
      .i_fetch_cmd(w_fetch),
      .i_load_cmd(w_load),
      .o_debug_fifo_push(),
      .o_debug_fifo_pull(),
      .o_debug_smi_test(),
      .o_debug_loopback_tx(w_debug_lb_tx),
      .o_tx_sample_gap(tx_sample_gap),
  );

  wire w_debug_fifo_push;
  wire w_debug_fifo_pull;
  wire w_debug_smi_test;
  wire w_debug_lb_tx;
  wire [3:0] tx_sample_gap;

  // IO CTRL
  io_ctrl io_ctrl_ins (
      .i_rst_b(i_rst_b),
      .i_sys_clk(w_clock_sys),
      .i_ioc(w_ioc),
      .i_data_in(w_rx_data),
      .o_data_out(w_tx_data_io),
      .i_cs(w_cs[1]),
      .i_fetch_cmd(w_fetch),
      .i_load_cmd(w_load),

      // Digital interfaces
      .i_button(i_button),
      .i_config(i_config),
      .o_led0  (/*o_led0*/),
      .o_led1  (/*o_led1*/),
      .o_pmod  (),

      // Analog interfaces
      .o_mixer_fm(/*o_mixer_fm*/),
      .o_rx_h_tx_l(o_rx_h_tx_l),
      .o_rx_h_tx_l_b(o_rx_h_tx_l_b),
      .o_tr_vc1(o_tr_vc1),
      .o_tr_vc1_b(o_tr_vc1_b),
      .o_tr_vc2(o_tr_vc2),
      .o_shdn_tx_lna(o_shdn_tx_lna),
      .o_shdn_rx_lna(o_shdn_rx_lna),
      .o_mixer_en(/*o_mixer_en*/)
  );

  //=========================================================================
  // CONBINATORIAL ASSIGNMENTS
  //=========================================================================
  //assign w_clock_sys = r_counter;
    SB_GB clock_Global_Buffer_i ( // Required for a user‟s internally generated FPGA signal that is heavily loaded
    //and requires global buffering. For example, a user‟s logic-generated clock.
    .USER_SIGNAL_TO_GLOBAL_BUFFER (r_counter),
    .GLOBAL_BUFFER_OUTPUT ( w_clock_sys) );

  //=========================================================================
  // CLOCK AND DATA-FLOW
  //=========================================================================
  always @(posedge i_glob_clock) begin
    if (i_rst_b == 1'b0) begin
      r_counter <= 1'b0;
    end else begin
      r_counter <= !r_counter;

      case (w_cs)
        4'b0001: r_tx_data <= w_tx_data_sys;
        4'b0010: r_tx_data <= w_tx_data_io;
        4'b0100: r_tx_data <= w_tx_data_smi;
        4'b1000: r_tx_data <= 8'b10100101;  // 0xA5: reserved
        4'b0000: r_tx_data <= 8'b00000000;  // no module selected
      endcase
    end
  end

  //=========================================================================
  // I/O (SB_IO, SB_GB) DIFFERENTIAL LINES
  //=========================================================================

  //---------------------------------------------
  //  LVDS CLOCK
  //---------------------------------------------
  // Differential clock signal (DDR)
  wire lvds_clock;  // The direct clock input
  wire lvds_clock_buf;  // The clock input after global buffer (improved fanout)

  SB_IO #(
      .PIN_TYPE   (6'b000001),       // Input only, direct mode
      .IO_STANDARD("SB_LVDS_INPUT")
  )  // LVDS input
      iq_rx_clk (
      .PACKAGE_PIN(i_iq_rx_clk_p),  // Physical connection to 'i_iq_rx_clk_p'
      .D_IN_0(lvds_clock)
  );  // Wire out to 'lvds_clock'

  //assign lvds_clock_buf = lvds_clock;
    SB_GB My_Global_Buffer_i ( // Required for a user‟s internally generated FPGA signal that is heavily loaded
    //and requires global buffering. For example, a user‟s logic-generated clock.
    .USER_SIGNAL_TO_GLOBAL_BUFFER (lvds_clock),
    .GLOBAL_BUFFER_OUTPUT ( lvds_clock_buf) );
  //---------------------------------------------
  // LVDS RX - I/Q Data
  //---------------------------------------------

  // Differential 2.4GHz I/Q DDR signal
  SB_IO #(
      .PIN_TYPE   (6'b000000),        // Input only, DDR mode (sample on both pos edge and
      // negedge of the input clock)
      .IO_STANDARD("SB_LVDS_INPUT"),  // LVDS standard
      .NEG_TRIGGER(1'b0)
  )  // The signal is not negated
      iq_rx_24 (
      .PACKAGE_PIN(i_iq_rx_24_n),  // Attention: this is the 'n' input, thus the actual values
      //            will need to be negated (PCB layout constraint)
      .INPUT_CLK(lvds_clock_buf),  // The I/O sampling clock with DDR
      .D_IN_0(w_lvds_rx_24_d0),  // the 0 deg data output
      .D_IN_1(w_lvds_rx_24_d1)
  );  // the 180 deg data output

  // Differential 0.9GHz I/Q DDR signal
  SB_IO #(
      .PIN_TYPE   (6'b000000),        // Input only, DDR mode (sample on both pos edge and
      // negedge of the input clock)
      .IO_STANDARD("SB_LVDS_INPUT"),  // LVDS standard
      .NEG_TRIGGER(1'b0)
  )  // The signal is negated in hardware
      iq_rx_09 (
      .PACKAGE_PIN(i_iq_rx_09_p),
      .INPUT_CLK(lvds_clock_buf),  // The I/O sampling clock with DDR
      .D_IN_0(w_lvds_rx_09_d0),  // the 0 deg data output
      .D_IN_1(w_lvds_rx_09_d1)
  );  // the 180 deg data output

  //----------------------------------------------
  // LVDS TX - I/Q Data
  //----------------------------------------------

  // Non-inverting, P-side of pair
  SB_IO #(
      .PIN_TYPE   (6'b010000),   // {PIN_OUTPUT_DDR, PIN_INPUT_REGISTER }
      .IO_STANDARD("SB_LVCMOS"),
  ) iq_tx_p (
      .PACKAGE_PIN(o_iq_tx_p),
      .OUTPUT_CLK(lvds_clock_buf),
      .D_OUT_0(~w_lvds_tx_d0),
      .D_OUT_1(~w_lvds_tx_d1)
  );

  // Inverting, N-side of pair 
  SB_IO #(
      .PIN_TYPE   (6'b010000),   // {PIN_OUTPUT_DDR, PIN_INPUT_REGISTER }
      .IO_STANDARD("SB_LVCMOS"),
  ) iq_tx_n (
      .PACKAGE_PIN(o_iq_tx_n),
      .OUTPUT_CLK(lvds_clock_buf),
      .D_OUT_0(w_lvds_tx_d0),
      .D_OUT_1(w_lvds_tx_d1)
  );


  // generating clock with logic and a SB_IO. The PLL conflicts with pin A14
   
  SB_IO #(
      .PIN_TYPE   (6'b010000),   // {PIN_OUTPUT_DDR, PIN_INPUT_REGISTER }
      .IO_STANDARD("SB_LVCMOS"),
  ) iq_tx_clk_p (
      .PACKAGE_PIN(o_iq_tx_clk_p),
      .OUTPUT_CLK(lvds_clock_buf),
      .D_OUT_0(1'b1),
      .D_OUT_1(1'b0)
  );
  
  SB_IO #(
      .PIN_TYPE   (6'b010000),   // {PIN_OUTPUT_DDR, PIN_INPUT_REGISTER }
      .IO_STANDARD("SB_LVCMOS"),
  ) iq_tx_clk_n (
      .PACKAGE_PIN(o_iq_tx_clk_n),
      .OUTPUT_CLK(lvds_clock_buf),
      .D_OUT_0(1'b0),
      .D_OUT_1(1'b1)
  );
  

  //=========================================================================
  // LVDS RX SIGNAL FROM MODEM
  //=========================================================================
  wire w_lvds_rx_09_d0;  // 0 degree
  wire w_lvds_rx_09_d1;  // 180 degree
  wire w_lvds_rx_24_d0;  // 0 degree
  wire w_lvds_rx_24_d1;  // 180 degree

  // RX FIFO Internals
  wire w_rx_09_fifo_write_clk;
  wire w_rx_09_fifo_push;
  wire [31:0] w_rx_09_fifo_data;

  wire w_rx_24_fifo_write_clk;
  wire w_rx_24_fifo_push;
  wire [31:0] w_rx_24_fifo_data;

  lvds_rx lvds_rx_09_inst (
      .i_rst_b  (i_rst_b),
      .i_ddr_clk(lvds_clock_buf),
      .i_ddr_data({w_lvds_rx_09_d0, w_lvds_rx_09_d1}),
      .i_fifo_full(w_rx_fifo_full),
      .o_fifo_write_clk(w_rx_09_fifo_write_clk),
      .o_fifo_push(w_rx_09_fifo_push),

      .o_fifo_data  (w_rx_09_fifo_data),
      .i_sync_input (1'b0),
      .o_debug_state()
  );

  lvds_rx lvds_rx_24_inst (
      .i_rst_b  (i_rst_b),
      .i_ddr_clk(lvds_clock_buf),

      .i_ddr_data({!w_lvds_rx_24_d0, !w_lvds_rx_24_d1}),

      .i_fifo_full(w_rx_fifo_full),
      .o_fifo_write_clk(w_rx_24_fifo_write_clk),
      .o_fifo_push(w_rx_24_fifo_push),

      .o_fifo_data  (w_rx_24_fifo_data),
      .i_sync_input (1'b0),
      .o_debug_state()
  );

  wire w_rx_fifo_write_clk = lvds_clock_buf; //(channel == 1'b0) ? w_rx_09_fifo_write_clk : w_rx_24_fifo_write_clk;
  wire w_rx_fifo_push = (channel == 1'b0) ? w_rx_09_fifo_push : w_rx_24_fifo_push;
  wire [31:0] w_rx_fifo_data = (channel == 1'b0) ? w_rx_09_fifo_data : w_rx_24_fifo_data;
  wire w_rx_fifo_pull;
  wire [31:0] w_rx_fifo_pulled_data;
  wire w_rx_fifo_full;
  wire w_rx_fifo_empty;
  wire channel;

  //assign channel = i_smi_a3;

  complex_fifo  #(
      .ADDR_WIDTH(10),  // 1024 samples
      .DATA_WIDTH(16),  // 2x16 for I and Q 
  ) rx_fifo (
      .wr_rst_b_i(i_rst_b),
      .wr_clk_i(w_rx_fifo_write_clk),
      .wr_en_i(w_rx_fifo_push),
      .wr_data_i(w_rx_fifo_data),
      .rd_rst_b_i(i_rst_b),
      .rd_clk_i(w_clock_sys),
      .rd_en_i(w_rx_fifo_pull),
      .rd_data_o(w_rx_fifo_pulled_data),
      .full_o(w_rx_fifo_full),
      .empty_o(w_rx_fifo_empty),
      .debug_pull(1'b0/*w_debug_fifo_pull*/),
      .debug_push(1'b0/*w_debug_fifo_push*/)
  );
  
  //=========================================================================
  // LVDS TX SIGNAL TO MODEM
  //=========================================================================
  wire w_lvds_tx_d0;  // 0 degree
  wire w_lvds_tx_d1;  // 180 degree
  
  lvds_tx lvds_tx_inst (
      .i_rst_b(i_rst_b),
      .i_ddr_clk(~lvds_clock_buf),
      .o_ddr_data({w_lvds_tx_d0, w_lvds_tx_d1}),
      .i_fifo_empty(w_tx_fifo_empty),
      .o_fifo_read_clk(w_tx_fifo_read_clk),
      .o_fifo_pull(w_tx_fifo_pull),
      .i_fifo_data(w_tx_fifo_pulled_data),
      .i_sample_gap(tx_sample_gap),
      .i_tx_state(~w_smi_data_direction),
      .i_sync_input(1'b0),
      .i_debug_lb(w_debug_lb_tx), 
      .o_tx_state_bit(),
      .o_sync_state_bit(),
  );
  
  wire w_tx_fifo_full;
  wire w_tx_fifo_empty;
  wire w_tx_fifo_read_clk;
  wire w_tx_fifo_push;
  wire w_tx_fifo_clock;
  wire [31:0] w_tx_fifo_data;
  wire w_tx_fifo_pull;
  wire [31:0] w_tx_fifo_pulled_data;
  
  complex_fifo #(
      .ADDR_WIDTH(10),  // 1024 samples
      .DATA_WIDTH(16),  // 2x16 for I and Q 
  ) tx_fifo (
      // smi clock is writing
      .wr_rst_b_i(i_rst_b),
      .wr_clk_i(w_tx_fifo_clock),
      .wr_en_i(w_tx_fifo_push),
      .wr_data_i(w_tx_fifo_data),
	  
      // lvds clock is pulling (reading)
      .rd_rst_b_i(i_rst_b),
      .rd_clk_i(w_tx_fifo_read_clk),
      .rd_en_i(w_tx_fifo_pull),
      .rd_data_o(w_tx_fifo_pulled_data),
      .full_o(w_tx_fifo_full),
      .empty_o(w_tx_fifo_empty),
      .debug_pull(1'b0),
      .debug_push(1'b0)
  );

  smi_ctrl smi_ctrl_ins (
      .i_rst_b(i_rst_b),
      .i_sys_clk(w_clock_sys),
      .i_ioc(w_ioc),
      .i_data_in(w_rx_data),
      .o_data_out(w_tx_data_smi),
      .i_cs(w_cs[2]),
      .i_fetch_cmd(w_fetch),
      .i_load_cmd(w_load),

      // FIFO RX
      .o_rx_fifo_pull(w_rx_fifo_pull),
      .i_rx_fifo_pulled_data(w_rx_fifo_pulled_data),
      .i_rx_fifo_empty(w_rx_fifo_empty),
      
      // FIFO TX
      .o_tx_fifo_push(w_tx_fifo_push),
      .o_tx_fifo_pushed_data(w_tx_fifo_data),
      .i_tx_fifo_full(w_tx_fifo_full),
      .o_tx_fifo_clock(w_tx_fifo_clock),

      .i_smi_soe_se(i_smi_soe_se),
      .i_smi_swe_srw(i_smi_swe_srw),
      .o_smi_data_out(w_smi_data_output),
      .i_smi_data_in(w_smi_data_input),
      .o_smi_read_req(w_smi_read_req),
      .o_smi_write_req(w_smi_write_req),
      .o_channel(channel),
      .o_dir (/*w_smi_data_direction*/),
      .i_smi_test(1'b0/*w_debug_smi_test*/),
      .o_cond_tx(),
      .o_address_error()
  );

  wire [7:0] w_smi_data_output;
  wire [7:0] w_smi_data_input;
  wire w_smi_read_req;
  wire w_smi_write_req;
  wire w_smi_data_direction;
  wire w_smi_data_output_enable;

  // the "Writing" flag indicates that the data[7:0] direction (inout)
  // from the FPGA's SMI module should be "output". This happens when the
  // SMI is in the READ mode - data flows from the FPGA to the RPI (RX mode)
  // The address has the MSB '1' when in RX mode. otherwise (when IDLE or TX)
  // the data is high-z, which is the more "recessive" mode

  assign w_smi_data_direction = i_smi_a2;
  assign w_smi_data_output_enable = i_smi_a2 & !i_smi_soe_se;
  
  genvar k;
  generate
   for (k=0; k<8;k++) begin
  SB_IO #(
      .PIN_TYPE(6'b1010_01),
      .PULLUP  (1'b0)
  ) smi_io0 (
      .PACKAGE_PIN(io_smi_data[k]),
      .OUTPUT_ENABLE(w_smi_data_output_enable),
      .D_OUT_0(w_smi_data_output[k]),
      .D_IN_0(w_smi_data_input[k])
  );
  
  end
  endgenerate

  // We need the 'o_smi_write_req' to be 1 only when the direction is TX 
  // (w_smi_data_direction == 0) and the write fifo is not full
  assign o_smi_read_req  = (w_smi_data_direction) ? w_smi_read_req : w_smi_write_req;
  assign o_smi_write_req = 1'bZ;

  assign o_led0 = w_smi_data_direction;
  assign o_led1 = channel;

endmodule  // top
