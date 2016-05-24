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
module rw_manager_di_buffer (
	clock,
	data,
	rdaddress,
	wraddress,
	wren,
	q,
	clear);

	parameter DATA_WIDTH = 32;
	parameter ADDR_WIDTH = 4;
	parameter NUM_WORDS = 16;

	input	  clock;
	input	[DATA_WIDTH-1:0]  data;
	input	[ADDR_WIDTH-1:0]  rdaddress;
	input	[ADDR_WIDTH-1:0]  wraddress;
	input	  wren;
	output	[DATA_WIDTH-1:0]  q;
	input clear;
`ifndef ALTERA_RESERVED_QIS
// synopsys translate_off
`endif
	tri1	  clock;
	tri0	  wren;
`ifndef ALTERA_RESERVED_QIS
// synopsys translate_on
`endif

// synthesis translate_off

	reg	[DATA_WIDTH-1:0]  q;
	reg [DATA_WIDTH-1:0] mem [0:NUM_WORDS-1];
	integer i;
/*
	integer j;
    always @(posedge clock or posedge clear) begin
        if (clear) begin
			for (i = 0; i < NUM_WORDS; i = i + 1) begin
				for (j = 0; j < DATA_WIDTH/32; j = j+ 1) begin
					mem[i][32*j+:32] <= i*(DATA_WIDTH/32) + j;
				end
			end
		end
		else begin
			q <= mem[rdaddress];
		end
	end
*/

    always @(posedge clock or posedge clear) begin
        if (clear) begin
			for (i = 0; i < NUM_WORDS; i = i + 1) begin
				mem[i] <= 0;
			end
		end
		else begin
			if (wren)
				mem[wraddress] <= data;
			q <= mem[rdaddress];
		end
	end

// synthesis translate_on

// synthesis read_comments_as_HDL on
// 	wire [DATA_WIDTH-1:0] sub_wire0;
// 	wire [DATA_WIDTH-1:0] q = sub_wire0[DATA_WIDTH-1:0];
// 	
// 	altsyncram	altsyncram_component (
// 				.address_a (wraddress),
// 				.clock0 (clock),
// 				.data_a (data),
// 				.wren_a (wren),
// 				.address_b (rdaddress),
// 				.q_b (sub_wire0),
// 				.aclr0 (1'b0),
// 				.aclr1 (1'b0),
// 				.addressstall_a (1'b0),
// 				.addressstall_b (1'b0),
// 				.byteena_a (1'b1),
// 				.byteena_b (1'b1),
// 				.clock1 (1'b1),
// 				.clocken0 (1'b1),
// 				.clocken1 (1'b1),
// 				.clocken2 (1'b1),
// 				.clocken3 (1'b1),
// 				.data_b ({DATA_WIDTH{1'b1}}),
// 				.eccstatus (),
// 				.q_a (),
// 				.rden_a (1'b1),
// 				.rden_b ((rdaddress < NUM_WORDS) ? 1'b1 : 1'b0),
// 				.wren_b (1'b0));
// 	defparam
// 		altsyncram_component.address_aclr_b = "NONE",
// 		altsyncram_component.address_reg_b = "CLOCK0",
// 		altsyncram_component.clock_enable_input_a = "BYPASS",
// 		altsyncram_component.clock_enable_input_b = "BYPASS",
// 		altsyncram_component.clock_enable_output_b = "BYPASS",
// 		altsyncram_component.intended_device_family = "Stratix III",
// 		altsyncram_component.lpm_type = "altsyncram",
// 		altsyncram_component.numwords_a = NUM_WORDS,
// 		altsyncram_component.numwords_b = NUM_WORDS,
// 		altsyncram_component.operation_mode = "DUAL_PORT",
// 		altsyncram_component.outdata_aclr_b = "NONE",
// 		altsyncram_component.outdata_reg_b = "UNREGISTERED",
// 		altsyncram_component.power_up_uninitialized = "FALSE",
// 		altsyncram_component.ram_block_type = "MLAB",
// 		altsyncram_component.widthad_a = ADDR_WIDTH,
// 		altsyncram_component.widthad_b = ADDR_WIDTH,
// 		altsyncram_component.width_a = DATA_WIDTH,
// 		altsyncram_component.width_b = DATA_WIDTH,
// 		altsyncram_component.width_byteena_a = 1;
// synthesis read_comments_as_HDL off


endmodule

