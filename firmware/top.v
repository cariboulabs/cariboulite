`include "spi_if.v"
`include "sys_ctrl.v"
`include "io_ctrl.v"
`include "smi_ctrl.v"
`include "lvds_rx.v"
`include "complex_fifo.v"

module top(
      input i_glob_clock,

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
      input i_iq_rx_09_p,     // Paired with i_iq_rx_09_n - only the 'B' pins need to be specified
      input i_iq_rx_24_n,     // Paired with i_iq_rx_24_p - only the 'B' pins need to be specified
      input i_iq_rx_clk_p,    // Paired with i_iq_rx_clk_n - only the 'B' pins need to be specified

      // Note: The icestorm (specifically nextpnr) fails to build if both diff pins are constrained
      //       in the constrain file and the interface herein. Thus we need to take the out so that
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

      // SMI TO RPI
      input i_smi_a1,
      input i_smi_a2,
      input i_smi_a3,

      input i_smi_soe_se,
      input i_smi_swe_srw,
      inout [7:0] io_smi_data,
      output o_smi_write_req,
      output o_smi_read_req,

      // SPI
      input i_mosi,
      input i_sck,
      input i_ss,
      output o_miso );

   //=========================================================================
   // INNER SIGNALS
   //=========================================================================
   reg [1:0]   r_counter;
   wire        w_clock_spi;
   wire        w_clock_sys;
   wire [4:0]  w_ioc;
   wire [7:0]  w_rx_data;
   reg [7:0]   r_tx_data;
   wire [3:0]  w_cs;
   wire        w_fetch;
   wire        w_load;
   reg         r_reset;
   wire        w_soft_reset;

   wire [7:0]  w_tx_data_sys;
   wire [7:0]  w_tx_data_io;
   wire [7:0]  w_tx_data_smi;

   //=========================================================================
   // INITIAL STATE
   //=========================================================================
   initial begin
      r_counter = 2'b00;
      r_reset = 1'b0;
   end

   //=========================================================================
   // INSTANCES
   //=========================================================================
   spi_if spi_if_ins
   (
      .i_rst_b (w_soft_reset),
      .i_sys_clk (w_clock_spi),
      .o_ioc (w_ioc),
      .o_data_in (w_rx_data),
      .i_data_out (r_tx_data),
      .o_cs (w_cs),
      .o_fetch_cmd (w_fetch),
      .o_load_cmd (w_load),

      // SPI Interface
      .i_spi_sck (i_sck),
      .o_spi_miso (int_miso),
      .i_spi_mosi (i_mosi),
      .i_spi_cs_b (i_ss)
   );

   wire int_miso;
   assign o_miso = (i_ss)?1'bZ:int_miso;

   sys_ctrl sys_ctrl_ins
   (
      .i_reset (r_reset),
      .i_sys_clk (w_clock_sys),
      .i_ioc (w_ioc),
      .i_data_in (w_rx_data),
      .o_data_out (w_tx_data_sys),
      .i_cs (w_cs[0]),
      .i_fetch_cmd (w_fetch),
      .i_load_cmd (w_load),
      .o_soft_reset (w_soft_reset),

      .i_error_list ({o_address_error, 7'b0000000})
   );

   io_ctrl io_ctrl_ins
   (
      .i_reset (w_soft_reset),
      .i_sys_clk (w_clock_sys),
      .i_ioc (w_ioc),
      .i_data_in (w_rx_data),
      .o_data_out (w_tx_data_io),
      .i_cs (w_cs[1]),
      .i_fetch_cmd (w_fetch),
      .i_load_cmd (w_load),

      /// Digital interfaces
      .i_button (i_button),
      .i_config (i_config),
      .o_led0 (o_led0),
      .o_led1 (o_led1),
      .o_pmod (),

      // Analog interfaces
      .o_mixer_fm (o_mixer_fm),
      .o_rx_h_tx_l (o_rx_h_tx_l),
      .o_rx_h_tx_l_b (o_rx_h_tx_l_b),
      .o_tr_vc1 (o_tr_vc1),
      .o_tr_vc1_b (o_tr_vc1_b),
      .o_tr_vc2 (o_tr_vc2),
      .o_shdn_tx_lna (o_shdn_tx_lna),
      .o_shdn_rx_lna (o_shdn_rx_lna),
      .o_mixer_en (o_mixer_en)
   );

   //=========================================================================
   // CONBINATORIAL ASSIGNMENTS
   //=========================================================================
   assign w_clock_spi = r_counter[0];
   assign w_clock_sys = r_counter[0];

   //=========================================================================
   // CLOCK AND DATA-FLOW
   //=========================================================================
   always @(posedge i_glob_clock)
   begin
      r_counter <= r_counter + 1;

      case (w_cs)
         4'b0001: r_tx_data <= w_tx_data_sys;
         4'b0010: r_tx_data <= w_tx_data_io;
         4'b0100: r_tx_data <= w_tx_data_smi;
         4'b1000: r_tx_data <= 8'b10100101;  // reserved
         4'b0000: r_tx_data <= 8'b00000000;  // no module selected
      endcase
   end

   //=========================================================================
   // I/O (SB_IO, SB_GB) DIFFERENTIAL LINES
   //=========================================================================
   // Differential clock signal
   wire lvds_clock;        // The direct clock input
   wire lvds_clock_buf;    // The clock input after global buffer (improved fanout)

   SB_IO #(
      .PIN_TYPE(6'b000001),         // Input only, direct mode
      .IO_STANDARD("SB_LVDS_INPUT") // LVDS input
   ) iq_rx_clk (
      .PACKAGE_PIN(i_iq_rx_clk_p),  // Physical connection to 'i_iq_rx_clk_p'
      .D_IN_0 ( lvds_clock ));      // Wire out to 'lvds_clock'

   SB_GB lvds_clk_buffer (          // Improve 'lvds_clock' fanout by pushing it into
                                    // a global high-fanout buffer
      .USER_SIGNAL_TO_GLOBAL_BUFFER (lvds_clock),
      .GLOBAL_BUFFER_OUTPUT(lvds_clock_buf) );

   // Differential 2.4GHz I/Q DDR signal
   SB_IO #(
      .PIN_TYPE(6'b000000),         // Input only, DDR mode (sample on both pos edge and
                                    // negedge of the input clock)
      .IO_STANDARD("SB_LVDS_INPUT"),// LVDS standard
      .NEG_TRIGGER(1'b0)            // We may need to specify it as 1'b1 to negate the signal
   ) iq_rx_24 (
      .PACKAGE_PIN(i_iq_rx_24_n),   // Attention: this is the 'n' input, thus the actual values
                                    //            will need to be negated (PCB layout constraint)
      .INPUT_CLK (lvds_clock_buf),  // The I/O sampling clock with DDR
      .D_IN_0 ( w_lvds_rx_24_d0 ),  // the 0 deg data output
      .D_IN_1 ( w_lvds_rx_24_d1 ) );// the 180 deg data output

   // Differential 0.9GHz I/Q DDR signal
   SB_IO #(
      .PIN_TYPE(6'b000000),         // Input only, DDR mode (sample on both pos edge and
                                    // negedge of the input clock)
      .IO_STANDARD("SB_LVDS_INPUT") // LVDS standard
   ) iq_rx_09 (
      .PACKAGE_PIN(i_iq_rx_09_p),
      .INPUT_CLK (lvds_clock_buf),  // The I/O sampling clock with DDR
      .D_IN_0 ( w_lvds_rx_09_d0 ),  // the 0 deg data output
      .D_IN_1 ( w_lvds_rx_09_d1 ) );// the 180 deg data output


   //=========================================================================
   // LVDS RX SIGNAL FROM MODEM
   //=========================================================================
   wire w_lvds_rx_09_d0;   // 0 degree
   wire w_lvds_rx_09_d1;   // 180 degree
   wire w_lvds_rx_24_d0;   // 0 degree
   wire w_lvds_rx_24_d1;   // 180 degree

   wire w_rx_09_fifo_full;
   wire w_rx_09_fifo_empty;
   wire w_rx_09_fifo_write_clk;
   wire w_rx_09_fifo_push;
   wire [31:0] w_rx_09_fifo_data;
   wire w_rx_09_fifo_pull;
   wire [31:0] w_rx_09_fifo_pulled_data;

   wire w_rx_24_fifo_full;
   wire w_rx_24_fifo_empty;
   wire w_rx_24_fifo_write_clk;
   wire w_rx_24_fifo_push;
   wire [31:0] w_rx_24_fifo_data;
   wire w_rx_24_fifo_pull;
   wire [31:0] w_rx_24_fifo_pulled_data;

   lvds_rx lvds_rx_09_inst
   (
      .i_reset (w_soft_reset),
      .i_ddr_clk (lvds_clock_buf),
      .i_ddr_data ({w_lvds_rx_09_d0, w_lvds_rx_09_d1}),
      .i_fifo_full (w_rx_09_fifo_full),
      .o_fifo_write_clk (w_rx_09_fifo_write_clk),
      .o_fifo_push (w_rx_09_fifo_push),
      .o_fifo_data (w_rx_09_fifo_data)
   );

   complex_fifo rx_09_fifo(
      .wr_rst_i (w_soft_reset),
      .wr_clk_i (w_rx_09_fifo_write_clk),
      .wr_en_i (w_rx_09_fifo_push),
      .wr_data_i (w_rx_09_fifo_data),
      .rd_rst_i (w_soft_reset),
      .rd_clk_i (w_clock_sys),
      .rd_en_i (w_rx_09_fifo_pull),
      .rd_data_o (w_rx_09_fifo_pulled_data),
      .full_o (w_rx_09_fifo_full),
      .empty_o (w_rx_09_fifo_empty)
   );

   lvds_rx lvds_rx_24_inst
   (
      .i_reset (w_soft_reset),
      .i_ddr_clk (lvds_clock_buf),
      .i_ddr_data ({w_lvds_rx_24_d0, w_lvds_rx_24_d1}),
      .i_fifo_full (w_rx_24_fifo_full),
      .o_fifo_write_clk (w_rx_24_fifo_write_clk),
      .o_fifo_push (w_rx_24_fifo_push),
      .o_fifo_data (w_rx_24_fifo_data)
   );

   complex_fifo rx_24_fifo(
      .wr_rst_i (w_soft_reset),
      .wr_clk_i (w_rx_24_fifo_write_clk),
      .wr_en_i (w_rx_24_fifo_push),
      .wr_data_i (w_rx_24_fifo_data),
      .rd_rst_i (w_soft_reset),
      .rd_clk_i (w_clock_sys),
      .rd_en_i (w_rx_24_fifo_pull),
      .rd_data_o (w_rx_24_fifo_pulled_data),
      .full_o (w_rx_24_fifo_full),
      .empty_o (w_rx_24_fifo_empty)
   );

   smi_ctrl smi_ctrl_ins
   (
      .i_reset (w_soft_reset),
      .i_sys_clk (i_glob_clock),
      .i_ioc (w_ioc),
      .i_data_in (w_rx_data),
      .o_data_out (w_tx_data_smi),
      .i_cs (w_cs[2]),
      .i_fetch_cmd (w_fetch),
      .i_load_cmd (w_load),

      .o_fifo_09_pull (w_rx_09_fifo_pull),
      .i_fifo_09_pulled_data (w_rx_09_fifo_pulled_data),
      .i_fifo_09_full (w_rx_09_fifo_full),
      .i_fifo_09_empty (w_rx_09_fifo_empty),

      .o_fifo_24_pull (w_rx_24_fifo_pull),
      .i_fifo_24_pulled_data (w_rx_24_fifo_pulled_data),
      .i_fifo_24_full (w_rx_24_fifo_full),
      .i_fifo_24_empty (w_rx_24_fifo_empty),

      .i_smi_a (w_smi_addr),
      .i_smi_soe_se (i_smi_soe_se),
      .i_smi_swe_srw (i_smi_swe_srw),
      .o_smi_data_out (w_smi_data_output),
      .i_smi_data_in (w_smi_data_input),
      .o_smi_read_req (w_smi_read_req),
      .o_smi_write_req (w_smi_write_req),
      .o_smi_writing (w_smi_writing),
      .i_smi_test (w_smi_test),
      .o_address_error ()
   );

   wire [2:0] w_smi_addr;
   wire [7:0] w_smi_data_output;
   wire [7:0] w_smi_data_input;
   wire w_smi_read_req;
   wire w_smi_write_req;
   wire w_smi_writing;
   wire w_smi_test;

   //assign w_smi_data_output = 8'b10100101;
   assign w_smi_test = 1'b0;
   assign w_smi_addr = {i_smi_a3, i_smi_a2, i_smi_a1};
   assign io_smi_data = (w_smi_writing)?w_smi_data_output:1'bZ;
   assign w_smi_data_input = io_smi_data;
   assign o_smi_write_req = (w_smi_writing)?w_smi_write_req:1'bZ;
   assign o_smi_read_req = (w_smi_writing)?w_smi_read_req:1'bZ;

   // Testing - output the clock signal (positive and negative) to the PMOD
   assign io_pmod[0] = (w_smi_writing)?w_smi_read_req:1'bZ;
   assign io_pmod[1] = (w_smi_writing)?w_smi_write_req:1'bZ;
   assign io_pmod[2] = w_rx_09_fifo_empty;
   assign io_pmod[3] = i_smi_soe_se;
   assign io_pmod[7:4] = w_rx_09_fifo_data[5:2];

endmodule // top
