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

module rw_manager_dm_decoder(ck, reset_n, code, pattern);
	
	parameter AFI_RATIO 	= "";

	input ck;
	input reset_n;
	input [2:0] code;
	output [2 * AFI_RATIO - 1 : 0] pattern;

	reg [2:0] code_R;

	always @(posedge ck or negedge reset_n) begin
		if(~reset_n) begin
			code_R <= 3'b000;
		end
		else begin
			code_R <= code;
		end
	end
	
	
	assign pattern[0] = code_R[2];
	assign pattern[1] = code_R[1];

	generate
	if (AFI_RATIO == 2)	begin
	    assign pattern[2] = code_R[2] ^ code_R[0];
	    assign pattern[3] = code_R[1] ^ code_R[0];
	end else if (AFI_RATIO == 4) begin
	    assign pattern[2] = code_R[2] ^ code_R[0];
	    assign pattern[3] = code_R[1] ^ code_R[0];
	    assign pattern[4] = code_R[2];
	    assign pattern[5] = code_R[1];
	    assign pattern[6] = code_R[2] ^ code_R[0];
	    assign pattern[7] = code_R[1] ^ code_R[0];
	end
	endgenerate

endmodule
