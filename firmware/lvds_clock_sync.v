
/*
 *  Contribution by matteo serva
 *  https://github.com/matteoserva
 * 
 */
 
module lvds_clock_sync (
    input        i_rst_b,
    input        i_ddr_clk,
    input        i_sys_clk,        // FPGA Clock
    
    output reg   o_data_sbe_ddr,
    
    output       o_lvds_ready_ddr,
    output       o_lvds_ready_sys
);

    reg [3:0] r_phase_count;
    always @(posedge i_ddr_clk) begin
            r_phase_count <= r_phase_count + 1;
            
            if(r_phase_count == 4'd0) begin
                o_data_sbe_ddr <= 1'b1;
            end else begin
                o_data_sbe_ddr <= 1'b0;
            end
    end

    reg [2:0] lvds_heartbeat_synchronizer;
    always @(posedge i_sys_clk) begin
            lvds_heartbeat_synchronizer = {r_phase_count[3] , lvds_heartbeat_synchronizer[2:1]};
    end
    wire lvds_heartbeat_sbe;
    assign lvds_heartbeat_sbe = lvds_heartbeat_synchronizer[1] ^ lvds_heartbeat_synchronizer[0];
    

    reg [6:0] lvds_timeout_counter;
    wire lvds_heartbeat_lost;
    assign lvds_heartbeat_lost = lvds_timeout_counter[6];

    always @(posedge i_sys_clk) begin
        if (i_rst_b == 1'b0) begin
            lvds_timeout_counter <= 0;
        end else begin
            if (lvds_heartbeat_sbe) begin
                lvds_timeout_counter <= 0;
            end else if(!lvds_heartbeat_lost) begin
                lvds_timeout_counter <= lvds_timeout_counter + 1;
            end
        end
    end


    reg [9:0] lvds_start_delay_counter;
    assign lvds_ready = lvds_start_delay_counter[9];
    wire lvds_ready;

    always @(posedge i_ddr_clk or posedge lvds_heartbeat_lost) begin
        if (lvds_heartbeat_lost == 1'b1) begin
            lvds_start_delay_counter <= 0;
        end else begin
            if (!lvds_ready) begin
                lvds_start_delay_counter <= lvds_start_delay_counter + 1;
            end
            
        end
    end

    assign o_lvds_ready_ddr = lvds_ready;

    reg [1:0] lvds_ready_synchronizer;
    always @(posedge i_sys_clk) begin
            lvds_ready_synchronizer = {lvds_ready , lvds_ready_synchronizer[1]};
    end
    
    assign o_lvds_ready_sys = lvds_ready_synchronizer[0];
    
endmodule
