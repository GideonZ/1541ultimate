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


// ***************************************************************************
// File name: write_datapath.v
// ***************************************************************************

`timescale 1 ps / 1 ps

(* altera_attribute = "-name AUTO_SHIFT_REGISTER_RECOGNITION OFF" *)

module nios_mem_if_ddr2_emif_0_p0_write_datapath(
	pll_afi_clk,
	reset_n,
	force_oct_off,
	afi_dqs_en,
	phy_ddio_oct_ena,
	afi_wdata,
	afi_wdata_valid,
	afi_dm,
	phy_ddio_dq,
	phy_ddio_dqs_en,
	phy_ddio_wrdata_en,
	phy_ddio_wrdata_mask,
	seq_num_write_fr_cycle_shifts
);


parameter MEM_ADDRESS_WIDTH     = "";
parameter MEM_DM_WIDTH          = "";
parameter MEM_CONTROL_WIDTH     = "";
parameter MEM_DQ_WIDTH          = "";
parameter MEM_READ_DQS_WIDTH    = "";
parameter MEM_WRITE_DQS_WIDTH   = "";

parameter AFI_ADDRESS_WIDTH     = "";
parameter AFI_DATA_MASK_WIDTH   = "";
parameter AFI_CONTROL_WIDTH     = "";
parameter AFI_DATA_WIDTH        = "";
parameter AFI_DQS_WIDTH	        = "";

parameter NUM_WRITE_PATH_FLOP_STAGES = "";
parameter NUM_WRITE_FR_CYCLE_SHIFTS = "";

localparam RATE_MULT = 2;
localparam DQ_GROUP_WIDTH = MEM_DQ_WIDTH / MEM_WRITE_DQS_WIDTH;

localparam DM_GROUP_WIDTH = MEM_DM_WIDTH / MEM_WRITE_DQS_WIDTH;


input	pll_afi_clk;
input	reset_n;

input	[AFI_DQS_WIDTH-1:0] force_oct_off;
input	[AFI_DQS_WIDTH-1:0] afi_dqs_en;
output	[AFI_DQS_WIDTH-1:0] phy_ddio_oct_ena;
input	[AFI_DATA_WIDTH-1:0] afi_wdata;
input	[AFI_DQS_WIDTH-1:0] afi_wdata_valid;
input	[AFI_DATA_MASK_WIDTH-1:0] afi_dm;
output	[AFI_DATA_WIDTH-1:0] phy_ddio_dq;
output	[AFI_DQS_WIDTH-1:0] phy_ddio_dqs_en;
output	[AFI_DQS_WIDTH-1:0] phy_ddio_wrdata_en;
output	[AFI_DATA_MASK_WIDTH-1:0] phy_ddio_wrdata_mask;
input	[MEM_WRITE_DQS_WIDTH * 2 - 1:0] seq_num_write_fr_cycle_shifts;

wire [AFI_DQS_WIDTH-1:0] oct_ena_source = afi_dqs_en;

wire	[AFI_DQS_WIDTH-1:0] phy_ddio_dqs_en_pre_shift;
wire	[AFI_DQS_WIDTH-1:0] phy_ddio_oct_ena_pre_shift;
wire	[AFI_DATA_WIDTH-1:0] phy_ddio_dq_pre_shift;
wire	[AFI_DQS_WIDTH-1:0] phy_ddio_wrdata_en_pre_shift;
wire	[AFI_DATA_MASK_WIDTH-1:0] phy_ddio_wrdata_mask_pre_shift;

generate
genvar stage;
if (NUM_WRITE_PATH_FLOP_STAGES == 0)
begin
	wire [AFI_DQS_WIDTH-1:0] oct_ena_source_extended;
	nios_mem_if_ddr2_emif_0_p0_fr_cycle_extender oct_ena_source_extender(
		.clk (pll_afi_clk),
		.extend_by (2'b10),
		.reset_n (1'b1),
		.datain (oct_ena_source),
		.dataout (oct_ena_source_extended)
	);
	defparam oct_ena_source_extender.DATA_WIDTH = MEM_WRITE_DQS_WIDTH;
	
	assign phy_ddio_oct_ena_pre_shift = ~oct_ena_source_extended & ~force_oct_off;

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter afi_dqs_en_shifter(
		.clk (pll_afi_clk),
		.shift_by (2'b01),
		.reset_n (1'b1),
		.datain (afi_dqs_en),
		.dataout (phy_ddio_dqs_en_pre_shift)
	);
	defparam afi_dqs_en_shifter.DATA_WIDTH = MEM_WRITE_DQS_WIDTH;
	
	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter afi_wdata_shifter(
		.clk (pll_afi_clk),
		.shift_by (2'b01),
		.reset_n (1'b1),
		.datain (afi_wdata),
		.dataout (phy_ddio_dq_pre_shift)
	);
	defparam afi_wdata_shifter.DATA_WIDTH = (MEM_DQ_WIDTH * 2);  

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_extender afi_wdata_valid_extender(
		.clk (pll_afi_clk),
		.extend_by (2'b10),
		.reset_n (1'b1),
		.datain (afi_wdata_valid),
		.dataout (phy_ddio_wrdata_en_pre_shift)
	);
	defparam afi_wdata_valid_extender.DATA_WIDTH = MEM_WRITE_DQS_WIDTH;

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter afi_dm_shifter(
		.clk (pll_afi_clk),
		.shift_by (2'b01),
		.reset_n (1'b1),
		.datain (afi_dm),
		.dataout (phy_ddio_wrdata_mask_pre_shift)
	);
	defparam afi_dm_shifter.DATA_WIDTH = (MEM_DM_WIDTH * 2);	
end
else
begin
	reg [AFI_DATA_WIDTH-1:0] afi_wdata_r [NUM_WRITE_PATH_FLOP_STAGES-1:0];
	reg [AFI_DQS_WIDTH-1:0] afi_wdata_valid_r [NUM_WRITE_PATH_FLOP_STAGES-1:0] /* synthesis dont_merge */;
	reg [AFI_DQS_WIDTH-1:0] oct_ena_source_r [NUM_WRITE_PATH_FLOP_STAGES-1:0] /* synthesis dont_merge */;
	reg [AFI_DQS_WIDTH-1:0] afi_dqs_en_r [NUM_WRITE_PATH_FLOP_STAGES-1:0];

	// phy_ddio_wrdata_mask is tied low during calibration
	// the purpose of the assignment is to avoid Quartus from connecting the signal to the sclr pin of the flop
	// sclr pin is very slow and causes timing failures
	(* altera_attribute = {"-name ALLOW_SYNCH_CTRL_USAGE OFF"}*) reg [AFI_DATA_MASK_WIDTH-1:0] afi_dm_r [NUM_WRITE_PATH_FLOP_STAGES-1:0];

	for (stage = 0; stage < NUM_WRITE_PATH_FLOP_STAGES; stage = stage + 1)
	begin : stage_gen
		always @(posedge pll_afi_clk)
		begin
			oct_ena_source_r[stage]  <= (stage == 0) ? oct_ena_source  : oct_ena_source_r[stage-1];
			afi_wdata_r[stage]       <= (stage == 0) ? afi_wdata       : afi_wdata_r[stage-1];
			afi_wdata_valid_r[stage] <= (stage == 0) ? afi_wdata_valid : afi_wdata_valid_r[stage-1];
			afi_dm_r[stage]          <= (stage == 0) ? afi_dm          : afi_dm_r[stage-1];
			afi_dqs_en_r[stage]      <= (stage == 0) ? afi_dqs_en      : afi_dqs_en_r[stage-1];
		end
	end

	assign phy_ddio_dq_pre_shift = afi_wdata_r[NUM_WRITE_PATH_FLOP_STAGES-1];
	assign phy_ddio_wrdata_en_pre_shift = afi_wdata_valid_r[NUM_WRITE_PATH_FLOP_STAGES-1];
	assign phy_ddio_wrdata_mask_pre_shift = afi_dm_r[NUM_WRITE_PATH_FLOP_STAGES-1];
	assign phy_ddio_dqs_en_pre_shift = afi_dqs_en_r[NUM_WRITE_PATH_FLOP_STAGES-1];

	
	wire [AFI_DQS_WIDTH-1:0] oct_ena_source_extended;
	nios_mem_if_ddr2_emif_0_p0_fr_cycle_extender oct_ena_source_extender(
		.clk (pll_afi_clk),
		.reset_n (1'b1),
		.extend_by (2'b10),
		.datain ((NUM_WRITE_PATH_FLOP_STAGES == 1) ? oct_ena_source : oct_ena_source_r[NUM_WRITE_PATH_FLOP_STAGES - 2]),
		.dataout (oct_ena_source_extended)
	);
	defparam oct_ena_source_extender.DATA_WIDTH = MEM_WRITE_DQS_WIDTH;
	
	wire [AFI_DQS_WIDTH-1:0] oct_ena_source_extended_shifted;
	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter oct_ena_source_extended_shifter(
		.clk (pll_afi_clk),
		.shift_by (2'b01),
		.reset_n (1'b1),
		.datain (oct_ena_source_extended),
		.dataout (oct_ena_source_extended_shifted)
	);
	defparam oct_ena_source_extended_shifter.DATA_WIDTH = MEM_WRITE_DQS_WIDTH;
	
	assign phy_ddio_oct_ena_pre_shift = ~oct_ena_source_extended_shifted & ~force_oct_off;
	
end
endgenerate

generate
genvar i, t;
for (i=0; i<MEM_WRITE_DQS_WIDTH; i=i+1)
begin: bs_wr_grp
	wire [1:0] seq_num_write_fr_cycle_shifts_per_group = seq_num_write_fr_cycle_shifts[2 * (i + 1) - 1 : i * 2];
	
	wire [1:0] shift_fr_cycle = 
		(NUM_WRITE_FR_CYCLE_SHIFTS == 0) ? 	2'b00 : (
		(NUM_WRITE_FR_CYCLE_SHIFTS == 1) ? 	2'b01 : (
		(NUM_WRITE_FR_CYCLE_SHIFTS == 2) ? 	2'b10 : (
		(NUM_WRITE_FR_CYCLE_SHIFTS == 3) ? 	2'b11 : (
											seq_num_write_fr_cycle_shifts_per_group))));
											
	wire [AFI_DQS_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_oct_ena_pre_shift;
	wire [AFI_DQS_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_oct_ena;
	
	wire [AFI_DQS_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_dqs_en_pre_shift;
	wire [AFI_DQS_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_dqs_en;
	
	wire [AFI_DATA_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_dq_pre_shift;
	wire [AFI_DATA_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_dq;
	
	wire [AFI_DQS_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_wrdata_en_pre_shift;
	wire [AFI_DQS_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_wrdata_en;


	wire [AFI_DATA_MASK_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_wrdata_mask_pre_shift;
	wire [AFI_DATA_MASK_WIDTH / MEM_WRITE_DQS_WIDTH - 1:0] grp_wrdata_mask;

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter dq_shifter(
		.clk (pll_afi_clk),
		.shift_by (shift_fr_cycle),
		.reset_n (1'b1),
		.datain (grp_dq_pre_shift),
		.dataout (grp_dq)	
	);
	defparam dq_shifter.DATA_WIDTH = (DQ_GROUP_WIDTH * 2);
	
	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter wrdata_mask_shifter(
		.clk (pll_afi_clk),
		.shift_by (shift_fr_cycle),
		.reset_n (1'b1),
		.datain (grp_wrdata_mask_pre_shift),
		.dataout (grp_wrdata_mask)
	);
	defparam wrdata_mask_shifter.DATA_WIDTH = (DM_GROUP_WIDTH * 2);
	
	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter wrdata_en_shifter(
		.clk (pll_afi_clk),
		.shift_by (shift_fr_cycle),
		.reset_n (1'b1),
		.datain (grp_wrdata_en_pre_shift),
		.dataout (grp_wrdata_en)
	);
	defparam wrdata_en_shifter.DATA_WIDTH = 1;

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter dqs_en_shifter(
		.clk (pll_afi_clk),
		.shift_by (shift_fr_cycle),
		.reset_n (1'b1),
		.datain (grp_dqs_en_pre_shift),
		.dataout (grp_dqs_en)
	);
	defparam dqs_en_shifter.DATA_WIDTH = 1;


	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter oct_ena_shifter(
		.clk (pll_afi_clk),
		.shift_by (shift_fr_cycle),
		.reset_n (1'b1),
		.datain (grp_oct_ena_pre_shift),
		.dataout (grp_oct_ena)
	);
	defparam oct_ena_shifter.DATA_WIDTH = 1;  

	for (t=0; t<RATE_MULT*2; t=t+1)
	begin: extract_ddr_grp
		wire [DQ_GROUP_WIDTH-1:0] dq_t_pre_shift = phy_ddio_dq_pre_shift[DQ_GROUP_WIDTH * (i+1) + MEM_DQ_WIDTH * t - 1 : DQ_GROUP_WIDTH * i + MEM_DQ_WIDTH * t];
		assign grp_dq_pre_shift[(t+1) * DQ_GROUP_WIDTH - 1 : t * DQ_GROUP_WIDTH] = dq_t_pre_shift;
		
		wire [DQ_GROUP_WIDTH-1:0] dq_t = grp_dq[(t+1) * DQ_GROUP_WIDTH - 1 : t * DQ_GROUP_WIDTH];
		assign phy_ddio_dq[DQ_GROUP_WIDTH * (i+1) + MEM_DQ_WIDTH * t - 1 : DQ_GROUP_WIDTH * i + MEM_DQ_WIDTH * t] = dq_t;

		wire [DM_GROUP_WIDTH-1:0] wrdata_mask_t_pre_shift = phy_ddio_wrdata_mask_pre_shift[DM_GROUP_WIDTH * (i+1) + MEM_DM_WIDTH * t - 1 : DM_GROUP_WIDTH * i + MEM_DM_WIDTH * t];
		assign grp_wrdata_mask_pre_shift[(t+1) * DM_GROUP_WIDTH - 1 : t * DM_GROUP_WIDTH] = wrdata_mask_t_pre_shift;
		
		wire [DM_GROUP_WIDTH-1:0] wrdata_mask_t = grp_wrdata_mask[(t+1) * DM_GROUP_WIDTH - 1 : t * DM_GROUP_WIDTH];
		assign phy_ddio_wrdata_mask[DM_GROUP_WIDTH * (i+1) + MEM_DM_WIDTH * t - 1 : DM_GROUP_WIDTH * i + MEM_DM_WIDTH * t] = wrdata_mask_t;		
	end
	
	for (t=0; t<RATE_MULT; t=t+1)
	begin: extract_sdr_grp
		assign grp_oct_ena_pre_shift[t] = phy_ddio_oct_ena_pre_shift[i + MEM_WRITE_DQS_WIDTH * t];
		assign phy_ddio_oct_ena[i + MEM_WRITE_DQS_WIDTH * t] = grp_oct_ena[t];
		
		assign grp_dqs_en_pre_shift[t] = phy_ddio_dqs_en_pre_shift[i + MEM_WRITE_DQS_WIDTH * t];
		assign phy_ddio_dqs_en[i + MEM_WRITE_DQS_WIDTH * t] = grp_dqs_en[t];
		
		assign grp_wrdata_en_pre_shift[t] = phy_ddio_wrdata_en_pre_shift[i + MEM_WRITE_DQS_WIDTH * t];
		assign phy_ddio_wrdata_en[i + MEM_WRITE_DQS_WIDTH * t] = grp_wrdata_en[t];
		
	end
end
endgenerate

endmodule
