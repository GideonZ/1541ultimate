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

module rw_manager_ram

(

 data,
 rdaddress, 
 wraddress,
 wren, clock,
 q

);

parameter DATA_WIDTH=36;
parameter ADDR_WIDTH=8;

input [(DATA_WIDTH-1):0] data;
input [(ADDR_WIDTH-1):0] rdaddress, wraddress;
input wren, clock;
output reg [(DATA_WIDTH-1):0] q;


	reg [DATA_WIDTH-1:0] ram[2**ADDR_WIDTH-1:0];


	always @ (posedge clock)

	begin

		if (wren)

		ram[wraddress] <= data[DATA_WIDTH-1:0];

		q <= ram[rdaddress];

	end



endmodule


