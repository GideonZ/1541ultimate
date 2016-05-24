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

module nios_mem_if_ddr2_emif_0_p0_reset_sync(
	reset_n,
	clk,
	reset_n_sync
);

parameter RESET_SYNC_STAGES = 4;
parameter NUM_RESET_OUTPUT = 1;

input	reset_n;
input	clk;
output	[NUM_RESET_OUTPUT-1:0] reset_n_sync;




// identify the synchronizer chain so that Quartus can analyze metastability.
// Since these resets are localized to the PHY alone, make them routed locally
// to avoid using global networks.
(* altera_attribute = {"-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS; -name GLOBAL_SIGNAL OFF"}*) reg	[RESET_SYNC_STAGES+NUM_RESET_OUTPUT-2:0] reset_reg /*synthesis dont_merge */;

generate
genvar i;
	for (i=0; i<RESET_SYNC_STAGES+NUM_RESET_OUTPUT-1; i=i+1)
	begin: reset_stage
		always @(posedge clk or negedge reset_n)
		begin
			if (~reset_n)
				reset_reg[i] <= 1'b0;
			else
			begin
				if (i==0)
					reset_reg[i] <= 1'b1;
				else if (i < RESET_SYNC_STAGES)
					reset_reg[i] <= reset_reg[i-1];
				else
					reset_reg[i] <= reset_reg[RESET_SYNC_STAGES-2];
				
			end
		end
	end
endgenerate

	assign reset_n_sync = reset_reg[RESET_SYNC_STAGES+NUM_RESET_OUTPUT-2:RESET_SYNC_STAGES-1];

endmodule
