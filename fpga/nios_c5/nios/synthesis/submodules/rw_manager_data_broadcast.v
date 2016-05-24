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

module rw_manager_data_broadcast(
	dq_data_in,
	dm_data_in,
	dq_data_out,
	dm_data_out
);

	parameter NUMBER_OF_DQS_GROUPS 	= "";
	parameter NUMBER_OF_DQ_PER_DQS 	= "";
	parameter AFI_RATIO 		= "";
	parameter MEM_DM_WIDTH		= "";

	localparam NUMBER_OF_DQ_BITS = NUMBER_OF_DQS_GROUPS * NUMBER_OF_DQ_PER_DQS;
	localparam NUMBER_OF_WORDS = 2 * AFI_RATIO;

	input [NUMBER_OF_DQ_PER_DQS * NUMBER_OF_WORDS - 1 : 0] dq_data_in;
	input [NUMBER_OF_WORDS - 1 : 0] dm_data_in;
	output [NUMBER_OF_DQ_BITS * NUMBER_OF_WORDS - 1 : 0] dq_data_out;
	output [MEM_DM_WIDTH * 2 * AFI_RATIO - 1 : 0] dm_data_out;

	genvar gr, wr, dmbit;
	generate
		for(wr = 0; wr < NUMBER_OF_WORDS; wr = wr + 1)
		begin : word
			for(gr = 0; gr < NUMBER_OF_DQS_GROUPS; gr = gr + 1)
			begin : group
				assign dq_data_out[wr * NUMBER_OF_DQ_BITS + (gr + 1) * NUMBER_OF_DQ_PER_DQS - 1 : wr * NUMBER_OF_DQ_BITS + gr * NUMBER_OF_DQ_PER_DQS] = 
					dq_data_in[(wr + 1) * NUMBER_OF_DQ_PER_DQS - 1 : wr * NUMBER_OF_DQ_PER_DQS];
			end

			for(dmbit = 0; dmbit < MEM_DM_WIDTH; dmbit = dmbit + 1)
			begin : data_mask_bit
				assign dm_data_out[wr * MEM_DM_WIDTH + dmbit] = dm_data_in[wr];
			end
		end
	endgenerate
	
`ifdef ADD_UNIPHY_SIM_SVA
	assert property (@dm_data_in NUMBER_OF_DQS_GROUPS == MEM_DM_WIDTH) else
	    $error("%t, [DATA BROADCAST ASSERT] NUMBER_OF_DQS_GROUPS and MEM_DM_WIDTH mismatch, NUMBER_OF_DQS_GROUPS = %d, MEM_DM_WIDTH = %d", $time, NUMBER_OF_DQS_GROUPS, MEM_DM_WIDTH);
`endif

endmodule
