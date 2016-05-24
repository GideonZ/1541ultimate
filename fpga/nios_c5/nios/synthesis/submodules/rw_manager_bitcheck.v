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

module rw_manager_bitcheck(
	ck,
	reset_n,
	clear,
	enable,
	read_data,
	reference_data,
	mask,
	error_word
);

	parameter DATA_WIDTH 		= "";
	parameter AFI_RATIO		= "";

	localparam NUMBER_OF_WORDS = 2 * AFI_RATIO;
	localparam DATA_BUS_SIZE = DATA_WIDTH * NUMBER_OF_WORDS;

	input ck;
	input reset_n;
	input clear;
	input enable;
	input [DATA_BUS_SIZE - 1 : 0] read_data;
	input [DATA_BUS_SIZE - 1 : 0] reference_data;
	input [NUMBER_OF_WORDS - 1 : 0] mask;
	output [DATA_WIDTH - 1 : 0] error_word;

	reg [DATA_BUS_SIZE - 1 : 0] read_data_r;
	reg [DATA_WIDTH - 1 : 0] error_word;
	reg enable_r;
	wire [DATA_WIDTH - 1 : 0] error_compute;


	always @(posedge ck or negedge reset_n) begin
		if(~reset_n) begin
			error_word <= {DATA_WIDTH{1'b0}};
			read_data_r <= {DATA_BUS_SIZE{1'b0}};
			enable_r <= 1'b0;
		end
		else begin
			if(clear) begin
				error_word <= {DATA_WIDTH{1'b0}};
			end
			else if(enable_r) begin
				error_word <= error_word | error_compute;
			end
			read_data_r <= read_data;
			enable_r <= enable;
		end
	end

	genvar b;
	generate
		for(b = 0; b < DATA_WIDTH; b = b + 1) 
		begin : bit_loop
			if (AFI_RATIO == 4) begin
				assign error_compute[b] = 
					((read_data_r[b] ^ reference_data[b]) & ~mask[0]) |
					((read_data_r[b + DATA_WIDTH] ^ reference_data[b + DATA_WIDTH]) & ~mask[1]) |
					((read_data_r[b + 2 * DATA_WIDTH] ^ reference_data[b + 2 * DATA_WIDTH]) & ~mask[2]) |
					((read_data_r[b + 3 * DATA_WIDTH] ^ reference_data[b + 3 * DATA_WIDTH]) & ~mask[3]) |
					((read_data_r[b + 4 * DATA_WIDTH] ^ reference_data[b + 4 * DATA_WIDTH]) & ~mask[4]) |
					((read_data_r[b + 5 * DATA_WIDTH] ^ reference_data[b + 5 * DATA_WIDTH]) & ~mask[5]) |
					((read_data_r[b + 6 * DATA_WIDTH] ^ reference_data[b + 6 * DATA_WIDTH]) & ~mask[6]) |
					((read_data_r[b + 7 * DATA_WIDTH] ^ reference_data[b + 7 * DATA_WIDTH]) & ~mask[7]);
			end
			else if (AFI_RATIO == 2) begin
				assign error_compute[b] = 
					((read_data_r[b] ^ reference_data[b]) & ~mask[0]) |
					((read_data_r[b + DATA_WIDTH] ^ reference_data[b + DATA_WIDTH]) & ~mask[1]) |
					((read_data_r[b + 2 * DATA_WIDTH] ^ reference_data[b + 2 * DATA_WIDTH])  & ~mask[2])|
					((read_data_r[b + 3 * DATA_WIDTH] ^ reference_data[b + 3 * DATA_WIDTH]) & ~mask[3]);
			end
			else begin
				assign error_compute[b] = 
					((read_data_r[b] ^ reference_data[b]) & ~mask[0]) |
					((read_data_r[b + DATA_WIDTH] ^ reference_data[b + DATA_WIDTH]) & ~mask[1]);
			end
		end
	endgenerate

endmodule
