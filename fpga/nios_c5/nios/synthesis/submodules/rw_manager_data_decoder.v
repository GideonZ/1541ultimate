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

module rw_manager_data_decoder(
	ck, 
	reset_n, 
	code, 
	pattern
);

	parameter DATA_WIDTH 		= "";
	parameter AFI_RATIO 		= "";

	input ck;
	input reset_n;
	input [3:0] code;
	output [2 * DATA_WIDTH * AFI_RATIO - 1 : 0] pattern;

	reg [3:0] code_R;

	always @(posedge ck or negedge reset_n) begin
		if(~reset_n) begin
			code_R <= 4'b0000;
		end
		else begin
			code_R <= code;
		end
	end

	genvar j;
	generate
		for(j = 0; j < DATA_WIDTH; j = j + 1)
		begin : bit_pattern
			if(j % 2 == 0) begin
				assign pattern[j] = code_R[3];
				assign pattern[j + DATA_WIDTH] = code_R[2];
				if (AFI_RATIO == 2) begin
					assign pattern[j + 2 * DATA_WIDTH] = code_R[3] ^ code_R[1];
					assign pattern[j + 3 * DATA_WIDTH] = code_R[2] ^ code_R[1];
				end else if (AFI_RATIO == 4) begin 
					assign pattern[j + 2 * DATA_WIDTH] = code_R[3] ^ code_R[1];
					assign pattern[j + 3 * DATA_WIDTH] = code_R[2] ^ code_R[1];
					assign pattern[j + 4 * DATA_WIDTH] = code_R[3];
					assign pattern[j + 5 * DATA_WIDTH] = code_R[2];
					assign pattern[j + 6 * DATA_WIDTH] = code_R[3] ^ code_R[1];
					assign pattern[j + 7 * DATA_WIDTH] = code_R[2] ^ code_R[1];
				end
			end
			else begin
				assign pattern[j] = code_R[3] ^ code_R[0];
				assign pattern[j + DATA_WIDTH] = code_R[2] ^ code_R[0];
				if (AFI_RATIO == 2) begin
					assign pattern[j + 2 * DATA_WIDTH] = code_R[3] ^ code_R[1] ^ code_R[0];
					assign pattern[j + 3 * DATA_WIDTH] = code_R[2] ^ code_R[1] ^ code_R[0];
				end else if (AFI_RATIO == 4) begin 
					assign pattern[j + 2 * DATA_WIDTH] = code_R[3] ^ code_R[1] ^ code_R[0];
					assign pattern[j + 3 * DATA_WIDTH] = code_R[2] ^ code_R[1] ^ code_R[0];
					assign pattern[j + 4 * DATA_WIDTH] = code_R[3] ^ code_R[0];
					assign pattern[j + 5 * DATA_WIDTH] = code_R[2] ^ code_R[0];
					assign pattern[j + 6 * DATA_WIDTH] = code_R[3] ^ code_R[1] ^ code_R[0];
					assign pattern[j + 7 * DATA_WIDTH] = code_R[2] ^ code_R[1] ^ code_R[0];
				end
			end
		end
	endgenerate

endmodule
