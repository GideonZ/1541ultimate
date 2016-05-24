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

module rw_manager_lfsr72(
	clk, 
	nrst, 
	ena, 
	word
);

	input clk;
	input nrst;
	input ena;
	output reg [71:0] word;

	always @(posedge clk or negedge nrst) begin
		if(~nrst) begin
			word <= 72'hAAF0F0AA55F0F0AA55;
		end
		else if(ena) begin
			word[71] <= word[0];
			word[70:66] <= word[71:67];
			word[65] <= word[66] ^ word[0];
			word[64:25] <= word[65:26];
			word[24] <= word[25] ^ word[0];
			word[23:19] <= word[24:20];
			word[18] <= word[19] ^ word[0];
			word[17:0] <= word[18:1];
		end
	end

endmodule
