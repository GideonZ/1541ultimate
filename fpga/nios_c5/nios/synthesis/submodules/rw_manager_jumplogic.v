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

module rw_manager_jumplogic(
	ck,
	reset_n,
	cntr_value,
	cntr_load,
	reg_select,
	reg_load_select,
	jump_value,
	jump_load,
	jump_check,
	jump_taken,
	jump_address,
	cntr_3,



	jump_ptr_0_export, 	
	jump_ptr_1_export, 
	jump_cntr_0_export, 	
	jump_cntr_1_export 

);

	parameter DATA_WIDTH = 8;

	input ck;
	input reset_n;
	input [DATA_WIDTH-1:0] cntr_value;
	input cntr_load;
	input [1:0] reg_select;
	input [1:0] reg_load_select;
	input [DATA_WIDTH-1:0] jump_value;
	input jump_load;
	input jump_check;
	output jump_taken;
	output [DATA_WIDTH-1:0] jump_address;
	output [DATA_WIDTH-1:0] cntr_3;


	output	[7:0] jump_ptr_0_export; 	
	output  [7:0] jump_ptr_1_export; 
	output  [7:0] jump_cntr_0_export; 	
	output  [7:0] jump_cntr_1_export;

	reg [7:0] cntr [0:3];
	reg [7:0] cntr_shadow [0:3];
	reg [7:0] jump_pointers [0:3];

	assign jump_ptr_0_export = jump_pointers [0];
 	assign jump_ptr_1_export = jump_pointers [1];
	assign jump_cntr_0_export = cntr [0];
 	assign jump_cntr_1_export = cntr [1];

	wire [3:0] comparisons;

	assign jump_address = jump_pointers[reg_select];

	assign jump_taken = (jump_check & ~comparisons[reg_select]);

	assign cntr_3 = cntr[3];

	genvar c;
	generate
		for(c = 0; c < 4; c = c + 1) 
		begin : jumpcounter

			assign comparisons[c] = (cntr[c] == {DATA_WIDTH{1'b0}});
		
			always @(posedge ck or negedge reset_n) begin
				if(~reset_n) begin
					cntr[c] <= {DATA_WIDTH{1'b0}};
				end
				else if (cntr_load && reg_load_select == c) begin
					cntr[c] <= cntr_value;
				end
				else if (jump_check && reg_select == c) begin
					cntr[c] <= (comparisons[c]) ? cntr_shadow[c] : cntr[c] - 1'b1;
				end
			end
		end
	endgenerate

	always @(posedge ck or negedge reset_n) begin
		if(~reset_n) begin
			jump_pointers[0] <= {DATA_WIDTH{1'b0}};
			jump_pointers[1] <= {DATA_WIDTH{1'b0}};
			jump_pointers[2] <= {DATA_WIDTH{1'b0}};
			jump_pointers[3] <= {DATA_WIDTH{1'b0}};

			cntr_shadow[0] <= {DATA_WIDTH{1'b0}};
			cntr_shadow[1] <= {DATA_WIDTH{1'b0}};
			cntr_shadow[2] <= {DATA_WIDTH{1'b0}};
			cntr_shadow[3] <= {DATA_WIDTH{1'b0}};
		end
		else begin
			if(jump_load) begin
				jump_pointers[0] <= (reg_load_select == 2'b00)? jump_value : jump_pointers[0];
				jump_pointers[1] <= (reg_load_select == 2'b01)? jump_value : jump_pointers[1];
				jump_pointers[2] <= (reg_load_select == 2'b10)? jump_value : jump_pointers[2];
				jump_pointers[3] <= (reg_load_select == 2'b11)? jump_value : jump_pointers[3];
			end

			if(cntr_load) begin
				cntr_shadow[0] <= (reg_load_select == 2'b00)? cntr_value : cntr_shadow[0];
				cntr_shadow[1] <= (reg_load_select == 2'b01)? cntr_value : cntr_shadow[1];
				cntr_shadow[2] <= (reg_load_select == 2'b10)? cntr_value : cntr_shadow[2];
				cntr_shadow[3] <= (reg_load_select == 2'b11)? cntr_value : cntr_shadow[3];
			end
		end
	end

endmodule
