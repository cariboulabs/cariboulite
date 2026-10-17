module smi_ctrl
(
    input               i_rst_b,
    input               i_sys_clk,        // FPGA Clock

    input  [4:0]        i_ioc,
    input  [7:0]        i_data_in,
    output reg [7:0]    o_data_out,
    input               i_cs,
    input               i_fetch_cmd,
    input               i_load_cmd,

    // FIFO INTERFACE
    output              o_rx_fifo_pull,
    input  [31:0]       i_rx_fifo_pulled_data,
    input               i_rx_fifo_empty,
    
    output              o_tx_fifo_push,
    output reg [31:0]   o_tx_fifo_pushed_data,
    input               i_tx_fifo_full,
    output              o_tx_fifo_clock,

    // SMI INTERFACE
    input               i_smi_soe_se,
    input               i_smi_swe_srw,
    output reg [7:0]    o_smi_data_out,
    input  [7:0]        i_smi_data_in,
    output              o_smi_read_req,
    output              o_smi_write_req,
    output              o_channel,
    output              o_dir,

    // TX CONDITIONAL
    output reg          o_cond_tx,
    
    output wire [1:0]   o_state
);

    // ---------------------------------
    // MODULE SPECIFIC IOC LIST
    // ---------------------------------
    localparam ioc_module_version = 5'b00000;     // read only
    localparam ioc_fifo_status    = 5'b00001;     // read-only
    localparam ioc_channel_select = 5'b00010;
    localparam ioc_dir_select     = 5'b00011;

    localparam [7:0] module_version = 8'b00000001;

    // ---------------------------------------
    // CONTROL REGISTERS
    // ---------------------------------------
    reg r_channel, r_dir;

    assign o_channel = r_channel;
    assign o_dir     = r_dir;

    always @(posedge i_sys_clk or negedge i_rst_b) begin
        if (!i_rst_b) begin
            r_dir     <= 1'b0;
            r_channel <= 1'b0;
            o_data_out <= 8'h00;
        end else if (i_cs) begin
            // READ
            if (i_fetch_cmd) begin
                case (i_ioc)
                    ioc_module_version: o_data_out <= module_version;
                    ioc_fifo_status: begin
                        o_data_out[0]   <= i_rx_fifo_empty;
                        o_data_out[1]   <= i_tx_fifo_full;
                        o_data_out[2]   <= r_channel;
                        o_data_out[3]   <= 1'b0;
                        o_data_out[4]   <= r_dir;
                        o_data_out[7:5] <= 3'b000;
                    end
                    default: o_data_out <= 8'h00;
                endcase
            end
            // WRITE
            else if (i_load_cmd) begin
                case (i_ioc)
                    ioc_channel_select: r_channel <= i_data_in[0];
                    ioc_dir_select:     r_dir     <= i_data_in[0];
                    default: ; // no-op
                endcase
            end
        end
    end

    // ---------------------------------------
    // RX SIDE (FPGA -> Pi)  -- original semantics kept
    // ---------------------------------------
    reg [4:0]  int_cnt_rx;              // 0,8,16,24 wrap
    reg        r_fifo_pull, r_fifo_pull_1;
    reg        w_fifo_pull_trigger;     // pulse on 2nd byte
    reg [31:0] r_fifo_pulled_data;

    wire soe_and_reset = i_rst_b & i_smi_soe_se;

    // Host can read whenever FIFO not empty (unchanged)
    assign o_smi_read_req = !i_rx_fifo_empty;

    // Make a single-cycle rd_en in sys domain using the 2-FF edge detect
    assign o_rx_fifo_pull = !r_fifo_pull_1 && r_fifo_pull && !i_rx_fifo_empty;

    // Byte emit on SOE falling edge; request next word while sending byte#1
    always @(negedge soe_and_reset or negedge i_rst_b) begin
        if (!i_rst_b) begin
            int_cnt_rx         <= 5'd0;
            r_fifo_pulled_data <= 32'h0000_0000;
            o_smi_data_out     <= 8'h00;
            w_fifo_pull_trigger<= 1'b0;
        end else begin
            // trigger FIFO pull on the *second* byte (int_cnt_rx==8)
            w_fifo_pull_trigger <= (int_cnt_rx == 5'd8);

            // drive current byte LSB->MSB order
            o_smi_data_out <= r_fifo_pulled_data[int_cnt_rx +: 8];

            // latch next 32b word right after sending the 4th byte (24)
            if (int_cnt_rx == 5'd24)
                r_fifo_pulled_data <= i_rx_fifo_pulled_data;

            // advance byte index: 0,8,16,24, wrap by 5b overflow
            int_cnt_rx <= int_cnt_rx + 5'd8;
        end
    end

    // sync the pull trigger into sys clock and form a 1-cycle pulse
    always @(posedge i_sys_clk or negedge i_rst_b) begin
        if (!i_rst_b) begin
            r_fifo_pull   <= 1'b0;
            r_fifo_pull_1 <= 1'b0;
        end else begin
            r_fifo_pull   <= w_fifo_pull_trigger;
            r_fifo_pull_1 <= r_fifo_pull;
        end
    end

// -----------------------------------------
// TX SIDE (Pi -> FPGA -> TX FIFO)  — compact
// -----------------------------------------
localparam [1:0] tx_b0 = 2'd0, tx_b1 = 2'd1, tx_b2 = 2'd2, tx_b3 = 2'd3;

// Board uses active-low SWE on the pin.
// Normalize so that "asserted" = 1 regardless of pin polarity.
parameter SWE_ACTIVE_HIGH = 0;                       // 0 = active-low on the pin
wire swe_in_norm = SWE_ACTIVE_HIGH ? i_smi_swe_srw   // active-high: use as-is
                                   : ~i_smi_swe_srw; // active-low: invert → asserted=1

assign o_smi_write_req = !i_tx_fifo_full;
assign o_tx_fifo_clock = i_sys_clk;

// 2-FF synchronize the normalized SWE, then edge-detect.
// We want to sample on the end of assertion:
//  - physical active-low: rising edge on the pin
//  - normalized (asserted=1): falling edge (1→0)
reg swe_q1, swe_q2, swe_q2_d;
always @(posedge i_sys_clk or negedge i_rst_b) begin
    if (!i_rst_b) begin
        swe_q1   <= 1'b0;
        swe_q2   <= 1'b0;
        swe_q2_d <= 1'b0;
    end else begin
        swe_q1   <= swe_in_norm;
        swe_q2   <= swe_q1;
        swe_q2_d <= swe_q2;
    end
end

// Falling edge of normalized SWE = end-of-byte strobe
wire swe_edge = (swe_q2_d & ~swe_q2);   // 1->0 on swe_in_norm


// Resync 8-bit bus (2FF) and snapshot once per byte at swe_edge
reg [7:0] d_q1, d_q2, d_q3, d_byte;
always @(posedge i_sys_clk) begin
    d_q1 <= i_smi_data_in;
    d_q2 <= d_q1;
    d_q3 <= d_q2;
    if (swe_edge) d_byte <= d_q3;
end

// Compact collector: shift register + 2-bit byte counter
reg  [31:0] frame_sr;   // {b3,b2,b1,b0} after 4 edges
reg  [1:0]  byte_ix;    // 0..3
reg         push_req;
reg         push_pulse;

assign o_tx_fifo_push = push_pulse;

// pack & push when allowed
always @(posedge i_sys_clk or negedge i_rst_b) begin
    if (!i_rst_b) begin
        frame_sr              <= 32'h0;
        byte_ix               <= 2'd0;
        o_tx_fifo_pushed_data <= 32'h0;
        o_cond_tx             <= 1'b0;
        push_req              <= 1'b0;
        push_pulse            <= 1'b0;
    end else begin
        push_pulse <= 1'b0;

        if (push_req && !i_tx_fifo_full) begin
            push_pulse <= 1'b1;
            push_req   <= 1'b0;
        end

        if (swe_edge) begin
            // place next byte into the shift register
            case (byte_ix)
                tx_b0: begin
                    frame_sr[7:0] <= d_byte;   // b0
                    // require SOF = 1 in b0[7]; else immediately push fallback
                    if (d_byte[7]) byte_ix <= tx_b1;
                    else begin
                        //o_tx_fifo_pushed_data <= 32'h8000_4000; // fallback "quiet"
                        push_req              <= 1'b1;
                        byte_ix               <= tx_b0;
                    end
                end
                tx_b1: begin
                    frame_sr[15:8] <= d_byte;  // b1
                    if (!d_byte[7]) byte_ix <= tx_b2;
                    else            byte_ix <= tx_b0;  // resync
                end
                tx_b2: begin
                    frame_sr[23:16] <= d_byte; // b2
                    if (!d_byte[7]) byte_ix <= tx_b3;
                    else            byte_ix <= tx_b0;  // resync
                end
                tx_b3: begin
                    frame_sr[31:24] <= d_byte; // b3
                    // full frame captured: require b3[7]==0
                    if (!d_byte[7] && frame_sr[7] && !frame_sr[15] && !frame_sr[23]) begin
                        // bytes now: b0=frame_sr[7:0], b1=frame_sr[15:8],
                        //            b2=frame_sr[23:16], b3=frame_sr[31:24]
                        // I = {b0[4:0], b1[6:0], b2[6]}
                        // Q = {b2[5:0], b3[6:0]}
                        o_tx_fifo_pushed_data <= {
                            2'b10,
                            frame_sr[4:0],  frame_sr[14:8], frame_sr[22],  // I[12:0]
                            1'b1,                                            // TX_EN held high
                            2'b01,
                            frame_sr[21:16], d_byte[6:0],                    // Q[12:0]
                            1'b0
                        };
                        //push_req <= 1'b1;
                    end else begin
                        //o_tx_fifo_pushed_data <= 32'h8000_4000;             // fallback
                    end
                    push_req <= 1'b1;
                    byte_ix  <= tx_b0;
                end
            endcase
        end
    end
end
// SMI write request mirrors FIFO backpressure
assign o_smi_write_req = !i_tx_fifo_full;

endmodule