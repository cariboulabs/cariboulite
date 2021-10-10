module complex_fifo #(
    parameter ADDR_WIDTH = 10,
    parameter DATA_WIDTH = 16
)
(
    input wire 			            wr_rst_i,
    input wire 			            wr_clk_i,
    input wire 			            wr_en_i,
    input wire [2*DATA_WIDTH-1:0]	wr_data_i,

    input wire 			            rd_rst_i,
    input wire 			            rd_clk_i,
    input wire 			            rd_en_i,
    output reg [2*DATA_WIDTH-1:0]	rd_data_o,

    output reg 			full_o,
    output reg 			empty_o
);

reg [ADDR_WIDTH-1:0]	wr_addr;
reg [ADDR_WIDTH-1:0]	rd_addr;

always @(posedge wr_clk_i) begin
    if (wr_rst_i) begin
        wr_addr <= 0;
        full_o <= 1'b0;
    end else begin
        if (wr_en_i) begin
            wr_addr <= wr_addr + 1'b1;
            full_o <= (wr_addr + 2) == rd_addr;
            mem_i[wr_addr] <= wr_data_i[31:16];
            mem_q[wr_addr] <= wr_data_i[15:0];
        end else begin
            full_o <= full_o & ((wr_addr + 1'b1) == rd_addr);
        end
    end
end

//reg [1:0] cnt;

always @(posedge rd_clk_i) begin
    if (rd_rst_i) begin
        rd_addr <= 0;
        empty_o <= 1'b1;
        //cnt <= 2'b00;
    end else begin
        if (rd_en_i) begin
            rd_addr <= rd_addr + 1'b1;
            empty_o <= (rd_addr + 1) == wr_addr;
            // big endien to little endien the following is the regular read, and it is 
            // followed by the converted form
            //rd_data_o[29:16] <= mem_i[rd_addr][13:0];
            //rd_data_o[15:0] <= mem_q[rd_addr];
            //rd_data_o[31:30] <= cnt;
            //cnt <= cnt + 1;
            rd_data_o[31:24] <= mem_q[rd_addr][7:0];
            rd_data_o[23:16] <= mem_q[rd_addr][15:8];
            rd_data_o[15:8] <= mem_i[rd_addr][7:0];
            rd_data_o[7:0] <= mem_i[rd_addr][15:8];
        end else begin
            empty_o <= empty_o & (rd_addr == wr_addr);
        end
    end
end

reg [DATA_WIDTH-1:0] mem_i[(1<<ADDR_WIDTH)-1:0];
reg [DATA_WIDTH-1:0] mem_q[(1<<ADDR_WIDTH)-1:0];

endmodule
