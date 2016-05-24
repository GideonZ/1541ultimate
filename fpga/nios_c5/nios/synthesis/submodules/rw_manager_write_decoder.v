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

module rw_manager_write_decoder(
	ck,
	reset_n,
	do_lfsr,
	dm_lfsr,
	do_lfsr_step,
	dm_lfsr_step,
	do_code,
	dm_code,
	do_data,
	dm_data
);

	parameter DATA_WIDTH = "";
	parameter AFI_RATIO	= "";

	localparam NUMBER_OF_WORDS = 2 * AFI_RATIO;
	localparam DO_LFSR_WIDTH = ((AFI_RATIO == 4) ? 72 : 36);

	input ck;
	input reset_n;
	input do_lfsr;
	input dm_lfsr;
	input do_lfsr_step;
	input dm_lfsr_step;
	input [3:0] do_code;
	input [2:0] dm_code;
	output [2 * DATA_WIDTH * AFI_RATIO - 1 : 0] do_data;
	output [NUMBER_OF_WORDS-1:0] dm_data;

	reg do_lfsr_r;
	reg dm_lfsr_r;

	wire [DO_LFSR_WIDTH-1:0] do_lfsr_word;
	wire [11:0] dm_lfsr_word;
	wire [2 * DATA_WIDTH * AFI_RATIO - 1 : 0] do_word;
	wire [NUMBER_OF_WORDS -1 : 0] dm_word;

	rw_manager_data_decoder DO_decoder(
		.ck(ck),
		.reset_n(reset_n),
		.code(do_code),
		.pattern(do_word)
	);
	defparam DO_decoder.DATA_WIDTH = DATA_WIDTH;
	defparam DO_decoder.AFI_RATIO = AFI_RATIO;

	rw_manager_dm_decoder DM_decoder_i(
		.ck(ck),
		.reset_n(reset_n),
		.code(dm_code),
		.pattern(dm_word)
	);
	defparam DM_decoder_i.AFI_RATIO = AFI_RATIO;

	generate
		begin
			if (AFI_RATIO == 4) begin
				rw_manager_lfsr72 do_lfsr_i(
					.clk(ck),
					.nrst(reset_n), 
					.ena(do_lfsr_step), 
					.word(do_lfsr_word)
				);
			end else begin
				rw_manager_lfsr36 do_lfsr_i(
					.clk(ck), 
					.nrst(reset_n), 
					.ena(do_lfsr_step), 
					.word(do_lfsr_word)
				);
			end
		end
	endgenerate

	rw_manager_lfsr12 dm_lfsr_i(
		.clk(ck), 
		.nrst(reset_n), 
		.ena(dm_lfsr_step), 
		.word(dm_lfsr_word)
	);

	always @(posedge ck or negedge reset_n) begin
		if(~reset_n) begin
			do_lfsr_r <= 1'b0;
			dm_lfsr_r <= 1'b0;
		end
		else begin
			do_lfsr_r <= do_lfsr;
			dm_lfsr_r <= dm_lfsr;
		end
	end

	assign do_data = (do_lfsr_r) ? do_lfsr_word[2 * DATA_WIDTH * AFI_RATIO - 1 : 0] : do_word;
	assign dm_data = (dm_lfsr_r) ? dm_lfsr_word[NUMBER_OF_WORDS+1: 2] : dm_word;

endmodule
