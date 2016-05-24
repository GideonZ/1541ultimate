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

module rw_manager_datamux(datain, sel, dataout);
	parameter DATA_WIDTH = 8;
	parameter SELECT_WIDTH = 1;
	parameter NUMBER_OF_CHANNELS = 2;

	input [NUMBER_OF_CHANNELS * DATA_WIDTH - 1 : 0] datain;
	input [SELECT_WIDTH - 1 : 0] sel;
	output [DATA_WIDTH - 1 : 0] dataout;

	wire [DATA_WIDTH - 1 : 0] vectorized_data [0 : NUMBER_OF_CHANNELS - 1];

	assign dataout = vectorized_data[sel];

	genvar c;
	generate
		for(c = 0 ; c < NUMBER_OF_CHANNELS ; c = c + 1) 
		begin : channel_iterator
			assign vectorized_data[c] = datain[(c + 1) * DATA_WIDTH - 1 : c * DATA_WIDTH];
		end
	endgenerate
	
`ifdef ADD_UNIPHY_SIM_SVA
	assert property (@datain NUMBER_OF_CHANNELS == 2**SELECT_WIDTH) else
	    $error("%t, [DATAMUX ASSERT] NUMBER_OF_CHANNELS PARAMETER is incorrect, NUMBER_OF_CHANNELS = %d, 2**SELECT_WIDTH = %d", $time, NUMBER_OF_CHANNELS, 2**SELECT_WIDTH);
`endif

endmodule
