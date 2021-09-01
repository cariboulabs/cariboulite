module spi_slave
  #(parameter SPI_MODE = 0)
  (
    // Control/Data Signals,
    input             i_sys_clk,        // FPGA Clock
    output reg        o_rx_data_valid,  // Data Valid pulse (1 clock cycle)
    output reg [7:0]  o_rx_byte,        // Byte received on MOSI
    input             i_tx_data_valid,  // Data Valid pulse to register i_tx_byte
    input [7:0]       i_tx_byte,        // Byte to serialize to MISO.

    // SPI Interface
    input             i_spi_sck,
    output reg        o_spi_miso,
    input             i_spi_mosi,
    input             i_spi_cs_b );

  reg [2:0] r_rx_bit_count;
  reg [2:0] r_tx_bit_count;
  reg [7:0] r_temp_rx_byte;
  reg [7:0] r_rx_byte;
  reg r_rx_done;
  reg r2_rx_done;
  reg r3_rx_done;
  reg [7:0] r_tx_byte;

  // Purpose: Recover SPI Byte in SPI Clock Domain
  // Samples line on correct edge of SPI Clock
  /*always @(posedge i_spi_sck or posedge i_spi_cs_b)
  begin
    if (i_spi_cs_b) begin
      r_rx_bit_count <= 0;
      r_rx_done <= 1'b0;
    end else begin
      r_rx_bit_count <= r_rx_bit_count + 1;
      r_temp_rx_byte <= {r_temp_rx_byte[6:0], i_spi_mosi};

      if (r_rx_bit_count == 3'b111) begin
        r_rx_done <= 1'b1;
        r_rx_byte <= {r_temp_rx_byte[6:0], i_spi_mosi};
      end else if (r_rx_bit_count == 3'b010) begin
        r_rx_done <= 1'b0;
      end
    end
  end
  */
  always @(posedge i_sys_clk)
  begin
    if (i_spi_cs_b) begin
      r_rx_bit_count <= 0;
      r_rx_done <= 1'b0;
    end else if (SCK_risingedge) begin
      r_rx_bit_count <= r_rx_bit_count + 1;
      r_temp_rx_byte <= {r_temp_rx_byte[6:0], i_spi_mosi};

      if (r_rx_bit_count == 3'b111) begin
        r_rx_done <= 1'b1;
        r_rx_byte <= {r_temp_rx_byte[6:0], i_spi_mosi};
      end else if (r_rx_bit_count == 3'b010) begin
        r_rx_done <= 1'b0;
      end
    end
  end

  // Purpose: Cross from SPI Clock Domain to main FPGA clock domain
  // Assert o_rx_data_valid for 1 clock cycle when o_rx_byte has valid data.
  always @(posedge i_sys_clk)
  begin
    // Here is where clock domains are crossed.
    // This will require timing constraint created, can set up long path.
    r2_rx_done <= r_rx_done;
    r3_rx_done <= r2_rx_done;
    if (r3_rx_done == 1'b0 && r2_rx_done == 1'b1) begin // rising edge
      o_rx_data_valid   <= 1'b1;  // Pulse Data Valid 1 clock cycle
      o_rx_byte <= r_rx_byte;
    end else begin
      o_rx_data_valid <= 1'b0;
    end
  end

  reg [2:0] SCKr;
  always @(posedge i_sys_clk) SCKr <= {SCKr[1:0], i_spi_sck};
  wire SCK_risingedge = (SCKr[2:1]==2'b01);  // now we can detect SCK rising edges

  // Purpose: Transmits 1 SPI Byte whenever SPI clock is toggling
  // Will transmit read data back to SW over MISO line.
  // Want to put data on the line immediately when CS goes low.
  always @(posedge i_sys_clk)
  begin
    if (i_spi_cs_b || i_tx_data_valid) begin
      r_tx_byte <= i_tx_byte;
      r_tx_bit_count <= 3'b110;
      o_spi_miso <= i_tx_byte[3'b111];  // Reset to MSb
    end else begin
      if (SCK_risingedge) begin
        r_tx_bit_count <= r_tx_bit_count - 1;
        o_spi_miso <= r_tx_byte[r_tx_bit_count];

        if (r_tx_bit_count == 3'b000) begin
          r_tx_byte <= 8'b00000000;
        end

      end
    end
  end
endmodule // SPI_Slave
