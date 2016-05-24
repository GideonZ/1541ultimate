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

module rw_manager_di_buffer_wrap(
	clock,
	data,
	rdaddress,
	wraddress,
	wren,
	q,
	clear);

	parameter DATA_WIDTH = 18;
	parameter READ_DATA_SIZE = 9;
	parameter WRITE_TO_READ_RATIO_2_EXPONENT = 2;
	parameter WRITE_TO_READ_RATIO = 1;
	parameter ADDR_WIDTH = 2;
	parameter NUM_WORDS = 16;

	input clock;
	input [DATA_WIDTH-1:0] data;
	input [ADDR_WIDTH - 1 : 0] rdaddress;
	input [ADDR_WIDTH-1:0] wraddress;
	input wren;
	output [READ_DATA_SIZE - 1 : 0] q;
	input clear;

	wire [DATA_WIDTH-1:0] q_wire;
	wire wren_gated;

	wire [ADDR_WIDTH + ADDR_WIDTH - WRITE_TO_READ_RATIO_2_EXPONENT - 1 : 0] rdaddress_tmp = {{ADDR_WIDTH{1'b0}}, rdaddress[ADDR_WIDTH-1 : WRITE_TO_READ_RATIO_2_EXPONENT]};

	assign wren_gated = (wraddress >= NUM_WORDS) ? 1'b0 : wren;

	rw_manager_di_buffer rw_manager_di_buffer_i(
		.clock(clock),
		.data(data),
		.rdaddress(rdaddress_tmp[ADDR_WIDTH-1 : 0]),
		.wraddress(wraddress),
		.wren(wren_gated),
		.q(q_wire),
		.clear(clear));
	defparam rw_manager_di_buffer_i.DATA_WIDTH = DATA_WIDTH;
	defparam rw_manager_di_buffer_i.ADDR_WIDTH = ADDR_WIDTH;
	defparam rw_manager_di_buffer_i.NUM_WORDS = NUM_WORDS;
	
	generate
		if(WRITE_TO_READ_RATIO_2_EXPONENT > 0) begin
			
			wire [WRITE_TO_READ_RATIO * READ_DATA_SIZE + DATA_WIDTH - 1 : 0] datain_tmp = {{(WRITE_TO_READ_RATIO * READ_DATA_SIZE){1'b0}}, q_wire};

			rw_manager_datamux rw_manager_datamux_i(
				.datain(datain_tmp[WRITE_TO_READ_RATIO*READ_DATA_SIZE-1:0]),
				.sel(rdaddress[WRITE_TO_READ_RATIO_2_EXPONENT - 1 : 0]),
				.dataout(q)
			);
			defparam rw_manager_datamux_i.DATA_WIDTH = READ_DATA_SIZE;
			defparam rw_manager_datamux_i.SELECT_WIDTH = WRITE_TO_READ_RATIO_2_EXPONENT;
			defparam rw_manager_datamux_i.NUMBER_OF_CHANNELS = WRITE_TO_READ_RATIO;
		end
		else begin
			assign q = q_wire;
		end
	endgenerate

endmodule
