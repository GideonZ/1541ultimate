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

module rw_manager_lfsr36(
	clk, 
	nrst, 
	ena, 
	word
);

	input clk;
	input nrst;
	input ena;
	output reg [35:0] word;

	always @(posedge clk or negedge nrst) begin
		if(~nrst) begin
			word <= 36'hF0F0AA55;
		end
		else if(ena) begin
			word[35] <= word[0];
			word[34] <= word[35];
			word[33] <= word[34];
			word[32] <= word[33];
			word[31] <= word[32];
			word[30] <= word[31];
			word[29] <= word[30];
			word[28] <= word[29];
			word[27] <= word[28];
			word[26] <= word[27];
			word[25] <= word[26];
			word[24] <= word[25] ^ word[0];
			word[23] <= word[24];
			word[22] <= word[23];
			word[21] <= word[22];
			word[20] <= word[21];
			word[19] <= word[20];
			word[18] <= word[19];
			word[17] <= word[18];
			word[16] <= word[17];
			word[15] <= word[16];
			word[14] <= word[15];
			word[13] <= word[14];
			word[12] <= word[13];
			word[11] <= word[12];
			word[10] <= word[11];
			word[9] <= word[10];
			word[8] <= word[9];
			word[7] <= word[8];
			word[6] <= word[7];
			word[5] <= word[6];
			word[4] <= word[5];
			word[3] <= word[4];
			word[2] <= word[3];
			word[1] <= word[2];
			word[0] <= word[1];
		end
	end

endmodule
