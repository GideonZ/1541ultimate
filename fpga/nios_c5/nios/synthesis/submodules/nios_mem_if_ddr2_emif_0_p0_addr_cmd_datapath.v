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

module nios_mem_if_ddr2_emif_0_p0_addr_cmd_datapath(
	clk,
	reset_n,
	afi_address,
	afi_bank,
	afi_cs_n,
	afi_cke,
	afi_odt,
	afi_ras_n,
	afi_cas_n,
	afi_we_n,
	phy_ddio_address,
	phy_ddio_bank,
	phy_ddio_cs_n,
	phy_ddio_cke,
	phy_ddio_we_n,
	phy_ddio_ras_n,
	phy_ddio_cas_n,
	phy_ddio_odt
);


parameter MEM_ADDRESS_WIDTH     = "";
parameter MEM_BANK_WIDTH        = "";
parameter MEM_CHIP_SELECT_WIDTH = "";
parameter MEM_CLK_EN_WIDTH 		= "";
parameter MEM_ODT_WIDTH 		= "";
parameter MEM_DM_WIDTH          = "";
parameter MEM_CONTROL_WIDTH     = "";
parameter MEM_DQ_WIDTH          = "";
parameter MEM_READ_DQS_WIDTH    = "";
parameter MEM_WRITE_DQS_WIDTH   = "";

parameter AFI_ADDRESS_WIDTH         = "";
parameter AFI_BANK_WIDTH            = "";
parameter AFI_CHIP_SELECT_WIDTH     = "";
parameter AFI_CLK_EN_WIDTH     		= "";
parameter AFI_ODT_WIDTH     		= "";
parameter AFI_DATA_MASK_WIDTH       = "";
parameter AFI_CONTROL_WIDTH         = "";
parameter AFI_DATA_WIDTH            = "";

parameter NUM_AC_FR_CYCLE_SHIFTS    = "";

localparam RATE_MULT = 2;


input	reset_n;
input	clk;
input	[AFI_ADDRESS_WIDTH-1:0]	afi_address;
input   [AFI_BANK_WIDTH-1:0] afi_bank;
input   [AFI_CHIP_SELECT_WIDTH-1:0] afi_cs_n;
input   [AFI_CLK_EN_WIDTH-1:0] afi_cke;
input   [AFI_ODT_WIDTH-1:0] afi_odt;
input   [AFI_CONTROL_WIDTH-1:0] afi_ras_n;
input   [AFI_CONTROL_WIDTH-1:0] afi_cas_n;
input   [AFI_CONTROL_WIDTH-1:0] afi_we_n;

output	[AFI_ADDRESS_WIDTH-1:0]	phy_ddio_address;
output	[AFI_BANK_WIDTH-1:0] phy_ddio_bank;
output	[AFI_CHIP_SELECT_WIDTH-1:0] phy_ddio_cs_n;
output	[AFI_CLK_EN_WIDTH-1:0] phy_ddio_cke;
output	[AFI_ODT_WIDTH-1:0] phy_ddio_odt;
output	[AFI_CONTROL_WIDTH-1:0] phy_ddio_ras_n;
output	[AFI_CONTROL_WIDTH-1:0] phy_ddio_cas_n;
output	[AFI_CONTROL_WIDTH-1:0] phy_ddio_we_n;

	wire [AFI_ADDRESS_WIDTH-1:0] afi_address_r = afi_address;
	wire [AFI_BANK_WIDTH-1:0] afi_bank_r = afi_bank;
	wire [AFI_CHIP_SELECT_WIDTH-1:0] afi_cs_n_r = afi_cs_n;
	wire [AFI_CLK_EN_WIDTH-1:0] afi_cke_r = afi_cke;
	wire [AFI_ODT_WIDTH-1:0] afi_odt_r = afi_odt;
	wire [AFI_CONTROL_WIDTH-1:0] afi_ras_n_r = afi_ras_n;
	wire [AFI_CONTROL_WIDTH-1:0] afi_cas_n_r = afi_cas_n;
	wire [AFI_CONTROL_WIDTH-1:0] afi_we_n_r = afi_we_n;


	wire [1:0] shift_fr_cycle =
		(NUM_AC_FR_CYCLE_SHIFTS == 0) ? 	2'b00 : (
		(NUM_AC_FR_CYCLE_SHIFTS == 1) ? 	2'b01 : (
		(NUM_AC_FR_CYCLE_SHIFTS == 2) ? 	2'b10 : (
											2'b11 )));

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter uaddr_cmd_shift_address(
		.clk (clk),
		.reset_n (reset_n),
		.shift_by (shift_fr_cycle),
		.datain (afi_address_r),
		.dataout (phy_ddio_address)
	);
	defparam uaddr_cmd_shift_address.DATA_WIDTH = MEM_ADDRESS_WIDTH;
	defparam uaddr_cmd_shift_address.REG_POST_RESET_HIGH = "false";




	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter uaddr_cmd_shift_bank(
		.clk (clk),
		.reset_n (reset_n),
		.shift_by (shift_fr_cycle),
		.datain (afi_bank_r),
		.dataout (phy_ddio_bank)
	);
	defparam uaddr_cmd_shift_bank.DATA_WIDTH = MEM_BANK_WIDTH;
	defparam uaddr_cmd_shift_bank.REG_POST_RESET_HIGH = "false";

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter uaddr_cmd_shift_cke(
		.clk (clk),
		.reset_n (reset_n),
		.shift_by (shift_fr_cycle),
		.datain (afi_cke_r),
		.dataout (phy_ddio_cke)
	);
	defparam uaddr_cmd_shift_cke.DATA_WIDTH = MEM_CLK_EN_WIDTH;
	defparam uaddr_cmd_shift_cke.REG_POST_RESET_HIGH = "false";

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter uaddr_cmd_shift_cs_n(
		.clk (clk),
		.reset_n (reset_n),
		.shift_by (shift_fr_cycle),
		.datain (afi_cs_n_r),
		.dataout (phy_ddio_cs_n)
	);
	defparam uaddr_cmd_shift_cs_n.DATA_WIDTH = MEM_CHIP_SELECT_WIDTH;
	defparam uaddr_cmd_shift_cs_n.REG_POST_RESET_HIGH = "true";

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter uaddr_cmd_shift_odt(
		.clk (clk),
		.reset_n (reset_n),
		.shift_by (shift_fr_cycle),
		.datain (afi_odt_r),
		.dataout (phy_ddio_odt)
	);
	defparam uaddr_cmd_shift_odt.DATA_WIDTH = MEM_ODT_WIDTH;
	defparam uaddr_cmd_shift_odt.REG_POST_RESET_HIGH = "false";

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter uaddr_cmd_shift_ras_n(
		.clk (clk),
		.reset_n (reset_n),
		.shift_by (shift_fr_cycle),
		.datain (afi_ras_n_r),
		.dataout (phy_ddio_ras_n)
	);
	defparam uaddr_cmd_shift_ras_n.DATA_WIDTH = MEM_CONTROL_WIDTH;
	defparam uaddr_cmd_shift_ras_n.REG_POST_RESET_HIGH = "true";

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter uaddr_cmd_shift_cas_n(
		.clk (clk),
		.reset_n (reset_n),
		.shift_by (shift_fr_cycle),
		.datain (afi_cas_n_r),
		.dataout (phy_ddio_cas_n)
	);
	defparam uaddr_cmd_shift_cas_n.DATA_WIDTH = MEM_CONTROL_WIDTH;
	defparam uaddr_cmd_shift_cas_n.REG_POST_RESET_HIGH = "true";

	nios_mem_if_ddr2_emif_0_p0_fr_cycle_shifter uaddr_cmd_shift_we_n(
		.clk (clk),
		.reset_n (reset_n),
		.shift_by (shift_fr_cycle),
		.datain (afi_we_n_r),
		.dataout (phy_ddio_we_n)
	);
	defparam uaddr_cmd_shift_we_n.DATA_WIDTH = MEM_CONTROL_WIDTH;
	defparam uaddr_cmd_shift_we_n.REG_POST_RESET_HIGH = "true";



endmodule
