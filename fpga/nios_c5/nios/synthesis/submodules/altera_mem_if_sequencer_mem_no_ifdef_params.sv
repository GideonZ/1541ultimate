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



// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 

module altera_mem_if_sequencer_mem_no_ifdef_params (
	clk1,
	reset1,
	clken1,
	s1_address,
	s1_be,
	s1_chipselect,
	s1_write,
	s1_writedata,
	s1_readdata
);

parameter AVL_ADDR_WIDTH = 0;
parameter AVL_DATA_WIDTH = 0;
parameter AVL_SYMBOL_WIDTH = 0;
parameter AVL_NUM_SYMBOLS = 0;
parameter MEM_SIZE = 0;
parameter INIT_FILE = "";
parameter RAM_BLOCK_TYPE = "";

localparam NUM_WORDS = MEM_SIZE / AVL_NUM_SYMBOLS;

input                            clk1;
input                            reset1;
input                            clken1;
input   [AVL_ADDR_WIDTH - 1:0]   s1_address;
input   [AVL_NUM_SYMBOLS - 1:0]  s1_be;
input                            s1_chipselect;
input                            s1_write;
input   [AVL_DATA_WIDTH - 1:0]   s1_writedata;
output  [AVL_DATA_WIDTH - 1:0]   s1_readdata;

wire             wren;
assign wren = s1_chipselect & s1_write;

	altsyncram the_altsyncram
	(
		.address_a (s1_address),
		.byteena_a (s1_be),
		.clock0 (clk1),
		.clocken0 (clken1),
		.data_a (s1_writedata),
		.q_a (s1_readdata),
		.wren_a (wren),
		.rden_a(),
		.rden_b(),
		.clocken2(),
		.clocken3(),
		.aclr0(),
		.aclr1(),
		.addressstall_a(),
		.addressstall_b(),
		.eccstatus(),
		.address_b (),
		.byteena_b (),
		.clock1 (),
		.clocken1 (),
		.data_b (),
		.q_b (),
		.wren_b ()
	);
	defparam the_altsyncram.byte_size = AVL_SYMBOL_WIDTH;
	defparam the_altsyncram.lpm_type = "altsyncram";
	defparam the_altsyncram.maximum_depth = NUM_WORDS;
	defparam the_altsyncram.numwords_a = NUM_WORDS;
	defparam the_altsyncram.outdata_reg_a = "UNREGISTERED";
	defparam the_altsyncram.ram_block_type = RAM_BLOCK_TYPE;
	defparam the_altsyncram.read_during_write_mode_mixed_ports = "DONT_CARE";
	defparam the_altsyncram.width_a = AVL_DATA_WIDTH;
	defparam the_altsyncram.width_byteena_a = AVL_NUM_SYMBOLS;
	defparam the_altsyncram.widthad_a = AVL_ADDR_WIDTH;
	defparam the_altsyncram.init_file = INIT_FILE;
	defparam the_altsyncram.operation_mode = "SINGLE_PORT";


endmodule

