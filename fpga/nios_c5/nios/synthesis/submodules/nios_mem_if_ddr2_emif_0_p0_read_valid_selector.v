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

module nios_mem_if_ddr2_emif_0_p0_read_valid_selector(
	reset_n,
	pll_afi_clk,
	latency_shifter,
	latency_counter,
	read_enable,
	read_valid
);

parameter MAX_LATENCY_COUNT_WIDTH	= "";

localparam LATENCY_NUM = 2**MAX_LATENCY_COUNT_WIDTH;

input	reset_n;
input	pll_afi_clk;
input	[LATENCY_NUM-1:0] latency_shifter;
input	[MAX_LATENCY_COUNT_WIDTH-1:0] latency_counter;
output	read_enable;
output	read_valid;

wire	[LATENCY_NUM-1:0] selector;
reg [LATENCY_NUM-1:0] selector_reg;
reg	read_enable;
reg reading_data;
reg	read_valid;

wire	[LATENCY_NUM-1:0] valid_select;


	lpm_decode	uvalid_select(
        .data (latency_counter),
        .eq (selector)
        // synopsys translate_off
        ,
        .aclr (),
        .clken (),
        .clock (),
        .enable ()
        // synopsys translate_on
	);

	defparam uvalid_select.lpm_decodes = LATENCY_NUM;
	defparam uvalid_select.lpm_type = "LPM_DECODE";
	defparam uvalid_select.lpm_width = MAX_LATENCY_COUNT_WIDTH;


	always @(posedge pll_afi_clk or negedge reset_n)
	begin
		if (~reset_n)
			selector_reg <= {LATENCY_NUM{1'b0}};
		else
			selector_reg <= selector;
	end

	assign valid_select = selector_reg & latency_shifter;


    always @(posedge pll_afi_clk or negedge reset_n)
    begin
        if (~reset_n)
        begin
            read_enable <= 1'b0;
            read_valid <= 1'b0;
        end
        else
        begin
            read_enable <= |valid_select;
	
            read_valid <= |valid_select;
        end
    end

endmodule
