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


// ******************************************************************************************************************************** 
// File name: fr_cycle_shifter.v
//
// The fr-cycle shifter shifts the input data by X number of full-rate-cycles, where X is specified by the shift_by port.
// datain is a bus that combines data of multiple full rate cycles, in specific time order. For example,
// in a quarter-rate system, the datain bus must be ordered as {T3, T2, T1, T0}, where Ty represents the y'th fr-cycle
// data item, of width DATA_WIDTH. The following illustrates outputs at the dataout port for various values of shift_by.
// "__" means don't-care.
//
// shift_by          dataout in current cycle            dataout in next clock cycle 
//    00                {T3, T2, T1, T0}                    {__, __, __, __}
//    01                {T2, T1, T0, __}                    {__, __, __, T3}
//    10                {T1, T0, __, __}                    {__, __, T3, T2}
//    11                {T0, __, __, __}                    {__, T3, T2, T1}
// 
// In full-rate or half-rate systems, only the least-significant bit of shift-by has an effect
// (i.e. you can only shift by 0 or 1 fr-cycle).
// In quarter-rate systems, all bits of shift_by are used (i.e. you can shift by 0, 1, 2, or 3 fr-cycles).
//
// ******************************************************************************************************************************** 

`timescale 1 ps / 1 ps

module nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter(
	clk,
	reset_n,
	shift_by,
	datain,
	dataout
);

// ******************************************************************************************************************************** 
// BEGIN PARAMETER SECTION
// All parameters default to "" will have their values passed in from higher level wrapper with the controller and driver 

parameter DATA_WIDTH = ""; 
parameter REG_POST_RESET_HIGH = "false";

localparam RATE_MULT = 2;
localparam FULL_DATA_WIDTH = DATA_WIDTH*RATE_MULT;

// END PARAMETER SECTION
// ******************************************************************************************************************************** 

input	clk;
input	reset_n;
input	[1:0] shift_by; 
input	[FULL_DATA_WIDTH-1:0] datain;
output	[FULL_DATA_WIDTH-1:0] dataout;


reg [FULL_DATA_WIDTH-1:0] datain_r;
always @(posedge clk or negedge reset_n)
begin
	if (~reset_n) begin
		if (REG_POST_RESET_HIGH == "true")
			datain_r <= {FULL_DATA_WIDTH{1'b1}};
		else
			datain_r <= {FULL_DATA_WIDTH{1'b0}};
	end else begin
		datain_r <= datain;
	end
end

wire [FULL_DATA_WIDTH-1:0] dataout_pre;


wire [DATA_WIDTH-1:0] datain_t0 = datain[(DATA_WIDTH*1)-1:(DATA_WIDTH*0)];
wire [DATA_WIDTH-1:0] datain_t1 = datain[(DATA_WIDTH*2)-1:(DATA_WIDTH*1)];
wire [DATA_WIDTH-1:0] datain_r_t1 = datain_r[(DATA_WIDTH*2)-1:(DATA_WIDTH*1)];

assign dataout = (shift_by[0] == 1'b1) ? {datain_t0, datain_r_t1} : {datain_t1, datain_t0};
	

endmodule
