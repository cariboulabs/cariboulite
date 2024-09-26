module smi_rx
(
    input               i_rst_b,
    input               i_sys_clk,        // FPGA Clock



    
    output           o_tx_fifo_push,
    output reg [15:0]   o_tx_fifo_pushed_data,
    input               i_tx_fifo_full,
    output              o_tx_fifo_clock,


    input               i_smi_swe_srw,
    input [7:0]         i_smi_data_in,
    output              o_smi_write_req,
    );

    reg o_cond_tx;
    
    // -----------------------------------------
    // TX SIDE
    // -----------------------------------------
    localparam
        tx_state_first  = 2'b00,
        tx_state_second = 2'b01,
        tx_state_third  = 2'b10,
        tx_state_fourth = 2'b11;

    reg [4:0] int_cnt_tx;
    reg [7:0] r_fifo_pushed_data;
    reg [1:0] tx_reg_state;
    reg modem_tx_ctrl;
    reg cond_tx_ctrl;
    reg r_fifo_push;
    reg r_fifo_push_1;
    reg w_fifo_push_trigger;
    wire swe_and_reset;
    
    assign o_smi_write_req = !i_tx_fifo_full;
    assign o_tx_fifo_push = (r_fifo_push_1 ^ r_fifo_push) && !i_tx_fifo_full;
    assign swe_and_reset = i_rst_b & i_smi_swe_srw;
    assign o_tx_fifo_clock = i_sys_clk;

    always @(negedge swe_and_reset)
    begin
        if (i_rst_b == 1'b0) begin
            tx_reg_state <= tx_state_first;
            w_fifo_push_trigger <= 1'b0;
            r_fifo_pushed_data <= 32'h00000000;
            modem_tx_ctrl <= 1'b0;
            cond_tx_ctrl <= 1'b0;
            //cnt <= 0;
        end else begin
            case (tx_reg_state)
                //----------------------------------------------
                tx_state_first: 
                begin
                    if (i_smi_data_in[0] == 1'b1) begin
                        r_fifo_pushed_data[7:0] <= i_smi_data_in[7:0];
                        modem_tx_ctrl <= i_smi_data_in[6];
                        cond_tx_ctrl <= i_smi_data_in[5];
                        tx_reg_state <= tx_state_second;
                    end else begin
                        // if from some reason we are in the first byte stage and we got
                        // a byte without '1' on its MSB, that means that we are not synced
                        // so push a "sync" word into the modem.
                        cond_tx_ctrl <= 1'b0;
                        modem_tx_ctrl <= 1'b0;
                    end
                end
                //----------------------------------------------
                tx_state_second: 
                begin

                        o_tx_fifo_pushed_data <= {i_smi_data_in[7:0],r_fifo_pushed_data};
                        tx_reg_state <= tx_state_third;

                        w_fifo_push_trigger <= 1'b1;
                end
                //----------------------------------------------
                tx_state_third: 
                begin
                    if (i_smi_data_in[0] == 1'b0) begin
                        r_fifo_pushed_data <= i_smi_data_in[7:0];
                        tx_reg_state <= tx_state_fourth;
                    end else begin
                        tx_reg_state <= tx_state_first;
                        w_fifo_push_trigger <= 1'b0;
                    end
                    
                end
                //----------------------------------------------
                tx_state_fourth: 
                begin

                        o_tx_fifo_pushed_data <= {i_smi_data_in[7:0],r_fifo_pushed_data};
                        w_fifo_push_trigger <= 1'b0;
                        o_cond_tx <= cond_tx_ctrl;
                        tx_reg_state <= tx_state_first;
                end
            endcase
        end
    end
    
    always @(posedge i_sys_clk)
    begin
        if (i_rst_b == 1'b0) begin
            r_fifo_push <= 1'b0;
            r_fifo_push_1 <= 1'b0;
        end else begin
            r_fifo_push <= w_fifo_push_trigger;
            r_fifo_push_1 <= r_fifo_push;
        end
    end

endmodule // smi_ctrl
