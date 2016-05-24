// (C) 2001-2015 Altera Corporation. All rights reserved.
// Your use of Altera Corporation's design tools, logic functions and other 
// software and tools, and its AMPP partner logic functions, and any output 
// files any of the foregoing (including device programming or simulation 
// files), and any associated documentation or information are expressly subject 
// to the terms and conditions of the Altera Program License Subscription 
// Agreement, Altera MegaCore Function License Agreement, or other applicable 
// license agreement, including, without limitation, that your use is for the 
// sole purpose of programming logic devices manufactured by Altera and sold by 
// Altera or its authorized distributors.  Please refer to the applicable 
// agreement for further details.


`timescale 1 ps / 1 ps

module rw_manager_ram_csr #(
   parameter DATA_WIDTH = 32,
   parameter ADDR_WIDTH = 2,
   parameter NUM_WORDS = 4
) (
   input                         csr_clk,       
   input                         csr_ena,       
   input                         csr_din,       

   input                         ram_clk,       
   input                         wren,          
   input      [(DATA_WIDTH-1):0] data,          
   input      [(ADDR_WIDTH-1):0] wraddress,     
   input      [(ADDR_WIDTH-1):0] rdaddress,     

   output reg [(DATA_WIDTH-1):0] q,             

   output reg                    csr_dout       
);


localparam integer DATA_COUNT = DATA_WIDTH*NUM_WORDS;

reg  [DATA_COUNT-1:0] all_data;
wire [DATA_COUNT-1:0] load_data;
wire [DATA_WIDTH-1:0] row_data [NUM_WORDS-1:0];

wire int_clk;

assign int_clk = (~csr_ena)? csr_clk : ram_clk;

always @(posedge int_clk)
begin
   if (~csr_ena)
      all_data <= {all_data[DATA_COUNT-2:0], csr_din};
   else if (wren)
      all_data <= load_data;
   else
      all_data <= all_data;

   q <= row_data[rdaddress];
end

always @(negedge csr_clk)
begin
   csr_dout <= all_data[DATA_COUNT-1];
end

generate

genvar i;


   for (i = 0; i < (NUM_WORDS); i = i + 1)
   begin: row_assign
     assign row_data[i] = all_data[(DATA_WIDTH*(i+1)-1) : (DATA_WIDTH*i)];
   end

endgenerate

generate

genvar j,k;

   for (j = 0; j < (NUM_WORDS); j = j + 1)
   begin: row
     for (k = 0; k < (DATA_WIDTH); k = k + 1)
     begin: column
        assign load_data[(DATA_WIDTH*j)+k] = (wraddress == j)? data[k] : all_data[(DATA_WIDTH*j)+k];
     end
   end
endgenerate

endmodule
