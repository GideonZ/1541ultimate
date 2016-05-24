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







// synopsys translate_off
`timescale 1 ps / 1 ps
// synopsys translate_on
module rw_manager_inst_ROM_no_ifdef_params (
	clock,
	data,
	rdaddress,
	wraddress,
	wren,
	q);

	parameter ROM_INIT_FILE_NAME = "inst_ROM.hex";

	input	  clock;
	input	[19:0]  data;
	input	[6:0]  rdaddress;
	input	[6:0]  wraddress;
	input	  wren;
	output	[19:0]  q;
`ifndef ALTERA_RESERVED_QIS
// synopsys translate_off
`endif
	tri1	  clock;
	tri0	  wren;
`ifndef ALTERA_RESERVED_QIS
// synopsys translate_on
`endif

	wire [19:0] sub_wire0;
	wire [19:0] q = sub_wire0[19:0];


	altsyncram	altsyncram_component (
				.clock0 (clock),
				.address_a (rdaddress),
				.wren_a (1'b0),
				.data_a ({20{1'b1}}),
				.q_a (sub_wire0),
				.address_b (),
				.q_b (),
				.aclr0 (1'b0),
				.aclr1 (1'b0),
				.addressstall_a (1'b0),
				.addressstall_b (1'b0),
				.byteena_a (1'b1),
				.byteena_b (1'b1),
				.clock1 (1'b1),
				.clocken0 (1'b1),
				.clocken1 (1'b1),
				.clocken2 (1'b1),
				.clocken3 (1'b1),
				.data_b ({20{1'b1}}),
				.eccstatus (),
				.rden_a (1'b1),
				.rden_b (1'b1),
				.wren_b (1'b0));
	defparam
		altsyncram_component.address_aclr_b = "NONE",
		altsyncram_component.address_reg_b = "CLOCK0",
		altsyncram_component.clock_enable_input_a = "BYPASS",
		altsyncram_component.clock_enable_input_b = "BYPASS",
		altsyncram_component.clock_enable_output_b = "BYPASS",
`ifdef NO_PLI
		altsyncram_component.init_file = "inst_ROM.rif",
`else
		altsyncram_component.init_file = ROM_INIT_FILE_NAME,
`endif
		altsyncram_component.intended_device_family = "Stratix III",
		altsyncram_component.lpm_type = "altsyncram",
		altsyncram_component.numwords_a = 128,
		altsyncram_component.numwords_b = 128,
		altsyncram_component.outdata_aclr_b = "NONE",
		altsyncram_component.outdata_reg_b = "UNREGISTERED",
		altsyncram_component.power_up_uninitialized = "FALSE",
		altsyncram_component.operation_mode = "ROM",
		altsyncram_component.ram_block_type = "MLAB",
		altsyncram_component.widthad_a = 7,
		altsyncram_component.widthad_b = 7,
		altsyncram_component.width_a = 20,
		altsyncram_component.width_b = 20,
		altsyncram_component.width_byteena_a = 1;


endmodule

