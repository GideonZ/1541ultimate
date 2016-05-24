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

module rw_manager_read_datapath(
	ck,
	reset_n,
	check_do,
	check_dm,
	check_do_lfsr,
	check_dm_lfsr,
	check_pattern_push,
	clear_error,
	read_data,
	read_data_valid,
	error_word,
	enable_ap_mode
);

	parameter DATA_WIDTH 		= "";
	parameter AFI_RATIO 		= "";

	localparam NUMBER_OF_WORDS = 2 * AFI_RATIO;
	localparam DATA_BUS_SIZE = DATA_WIDTH * NUMBER_OF_WORDS;

	input ck;
	input reset_n;

	input [3:0] check_do;
	input [2:0] check_dm;
	input check_do_lfsr;
	input check_dm_lfsr;
	input check_pattern_push;
	input enable_ap_mode;
	input clear_error;

	input [DATA_BUS_SIZE - 1 : 0] read_data;
	input read_data_valid;

	output [DATA_WIDTH - 1 : 0] error_word;

	reg [4:0] pattern_radd;
	reg [4:0] pattern_wadd;
	wire [4:0] pattern_radd_next;

	wire [8:0] check_word_write = { check_do, check_dm, check_do_lfsr, check_dm_lfsr };
	wire [8:0] check_word_read;
	wire [3:0] check_do_read = check_word_read[8:5];
	wire [2:0] check_dm_read = check_word_read[4:2];
	wire check_do_lfsr_read = check_word_read[1];
	wire check_dm_lfsr_read = check_word_read[0];

	wire [DATA_BUS_SIZE - 1 : 0] do_data;
	wire [NUMBER_OF_WORDS - 1 : 0] dm_data;

	wire do_lfsr_step = check_do_lfsr_read & read_data_valid;
	wire dm_lfsr_step = check_dm_lfsr_read & read_data_valid;


	reg h_data_valid;
	reg h_data_valid_r;
	
	
	always @(posedge ck)
	begin
		if (~reset_n)	
		begin
			h_data_valid <= 1'b0;
			h_data_valid_r <= 1'b0;
		end
		else
		begin
			if (h_data_valid)
				h_data_valid <= 1'b0;
			else if (read_data_valid & ~h_data_valid)
				h_data_valid <= 1'b1;
		
			h_data_valid_r <= h_data_valid;
	
		end
	end
	
	wire [DATA_BUS_SIZE - 1 : 0] h_do_data;
	assign h_do_data = (h_data_valid_r & enable_ap_mode)? {DATA_BUS_SIZE {1'b0}} : do_data; 


	rw_manager_bitcheck bitcheck_i(
		.ck(ck),
		.reset_n(reset_n),
		.clear(clear_error),
		.enable(read_data_valid),
		.read_data(read_data),
		.reference_data(h_do_data),
		.mask(dm_data),
		.error_word(error_word)
	);
	defparam bitcheck_i.DATA_WIDTH = DATA_WIDTH;
	defparam bitcheck_i.AFI_RATIO = AFI_RATIO;

	rw_manager_write_decoder write_decoder_i(
		.ck(ck),
		.reset_n(reset_n),
		.do_lfsr(check_do_lfsr_read),
		.dm_lfsr(check_dm_lfsr_read),
		.do_lfsr_step(do_lfsr_step),
		.dm_lfsr_step(dm_lfsr_step),
		.do_code(check_do_read),
		.dm_code(check_dm_read),
		.do_data(do_data),
		.dm_data(dm_data)
	);
	defparam write_decoder_i.DATA_WIDTH = DATA_WIDTH;
	defparam write_decoder_i.AFI_RATIO = AFI_RATIO;

	rw_manager_pattern_fifo pattern_fifo_i(
		.clock(ck),
		.data(check_word_write),
		.rdaddress(pattern_radd_next),
		.wraddress(pattern_wadd),
		.wren(check_pattern_push),
		.q(check_word_read)
	);


	assign pattern_radd_next = pattern_radd + (read_data_valid ? 1'b1 : 1'b0);

	always @(posedge ck or negedge reset_n) begin
		if(~reset_n) begin
			pattern_radd <= 5'b00000;
			pattern_wadd <= 5'b00000;
		end
		else begin
			if (clear_error) begin
			    pattern_radd <= 5'b00000;
			    pattern_wadd <= 5'b00000;
			end else begin
			    if(read_data_valid) begin
				    pattern_radd <= pattern_radd + 1'b1;
			    end
			    if(check_pattern_push) begin
				    pattern_wadd <= pattern_wadd + 1'b1;
			    end
			end
		end
	end

endmodule
