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

(* altera_attribute = "-name IP_TOOL_NAME altera_mem_if_ddr2_phy_core; -name IP_TOOL_VERSION 15.1; -name FITTER_ADJUST_HC_SHORT_PATH_GUARDBAND 100" *)
module nios_mem_if_ddr2_emif_0_p0 (
    global_reset_n,
    soft_reset_n,
	csr_soft_reset_req,
    parallelterminationcontrol,
    seriesterminationcontrol,
	pll_mem_clk,
	pll_mem_phy_clk,
	afi_phy_clk,
	pll_avl_phy_clk,
	pll_write_clk,
    pll_write_clk_pre_phy_clk,
	pll_addr_cmd_clk,
	pll_avl_clk,
	pll_config_clk,
	pll_locked,
    dll_pll_locked,
	dll_delayctrl,
	dll_clk,
	afi_reset_n,
	afi_reset_export_n,
	afi_clk,
	afi_half_clk,
	afi_addr,
	afi_ba,
	afi_cke,
	afi_cs_n,
	afi_ras_n,
	afi_we_n,
	afi_cas_n,
	afi_odt,
	afi_dqs_burst,
	afi_wdata,
	afi_wdata_valid,
	afi_dm,
	afi_rdata,
	afi_rdata_en,
	afi_rdata_en_full,
	afi_rdata_valid,
	afi_cal_success,
	afi_cal_fail,
	afi_wlat,
	afi_rlat,
	afi_mem_clk_disable,
	mem_a,
	mem_ba,
	mem_ck,
	mem_ck_n,
	mem_cke,
	mem_cs_n,
	mem_dm,
	mem_ras_n,
	mem_cas_n,
	mem_we_n,
	mem_dq,
	mem_dqs,
	mem_dqs_n,
	mem_odt,
	addr_cmd_clk,
	avl_clk,
	avl_reset_n,
	scc_clk,
	scc_reset_n,
	scc_data,
	scc_dqs_ena,
	scc_dqs_io_ena,
	scc_dq_ena,
	scc_dm_ena,
	scc_upd,
	capture_strobe_tracking,
	phy_clk,
	phy_reset_n,
	phy_read_latency_counter,
	phy_afi_wlat,
	phy_afi_rlat,
	phy_read_increment_vfifo_fr,
	phy_read_increment_vfifo_hr,
	phy_read_increment_vfifo_qr,
	phy_reset_mem_stable,
	phy_cal_debug_info,
	phy_read_fifo_reset,
	phy_vfifo_rd_en_override,
	phy_cal_success,
	phy_cal_fail,
	phy_read_fifo_q,
	calib_skip_steps
);


// ******************************************************************************************************************************** 
// BEGIN PARAMETER SECTION
// All parameters default to "" will have their values passed in from higher level wrapper with the controller and driver. 
parameter DEVICE_FAMILY = "Cyclone V";

// choose between abstract (fast) and regular model
`ifndef ALTERA_ALT_MEM_IF_PHY_FAST_SIM_MODEL
  `define ALTERA_ALT_MEM_IF_PHY_FAST_SIM_MODEL 0
`endif

parameter ALTERA_ALT_MEM_IF_PHY_FAST_SIM_MODEL = `ALTERA_ALT_MEM_IF_PHY_FAST_SIM_MODEL;

localparam FAST_SIM_MODEL = ALTERA_ALT_MEM_IF_PHY_FAST_SIM_MODEL;


// On-chip termination
parameter OCT_TERM_CONTROL_WIDTH   = 16;

// PHY-Memory Interface
// Memory device specific parameters, they are set according to the memory spec.
parameter MEM_IF_ADDR_WIDTH			= 14;
parameter MEM_IF_BANKADDR_WIDTH     = 2;
parameter MEM_IF_CK_WIDTH			= 1;
parameter MEM_IF_CLK_EN_WIDTH		= 1;
parameter MEM_IF_CS_WIDTH			= 1;
parameter MEM_IF_DM_WIDTH         	= 1;
parameter MEM_IF_CONTROL_WIDTH    	= 1; 
parameter MEM_IF_DQ_WIDTH         	= 8;
parameter MEM_IF_DQS_WIDTH         	= 1;
parameter MEM_IF_READ_DQS_WIDTH    	= 1;
parameter MEM_IF_WRITE_DQS_WIDTH   	= 1;
parameter MEM_IF_ODT_WIDTH         	= 1;

// PHY-Controller (AFI) Interface
// The AFI interface widths are derived from the memory interface widths based on full/half rate operations.
// The calculations are done on higher level wrapper.
parameter AFI_ADDR_WIDTH 	        = 28; 
parameter AFI_DM_WIDTH 	        	= 4; 
parameter AFI_BANKADDR_WIDTH        = 4; 
parameter AFI_CS_WIDTH				= 2;
parameter AFI_CLK_EN_WIDTH			= 2;
parameter AFI_CONTROL_WIDTH         = 2; 
parameter AFI_ODT_WIDTH             = 2; 
parameter AFI_DQ_WIDTH				= 32; 
parameter AFI_WRITE_DQS_WIDTH		= 2;
parameter AFI_RATE_RATIO			= 2;
parameter AFI_WLAT_WIDTH			= 6;
parameter AFI_RLAT_WIDTH			= 6;

// DLL Interface
parameter DLL_DELAY_CTRL_WIDTH	= 7;

parameter SCC_DATA_WIDTH            = 1;

parameter NUM_SUBGROUP_PER_READ_DQS        = 1;
parameter QVLD_EXTRA_FLOP_STAGES		   = 2;
parameter QVLD_WR_ADDRESS_OFFSET		   = 6;
	
// Read Datapath parameters, the values should not be changed unless the intention is to change the architecture.
// Read valid prediction FIFO
parameter READ_VALID_FIFO_SIZE             = 16;

// Data resynchronization FIFO
parameter READ_FIFO_SIZE                   = 8;

// Latency calibration parameters
parameter MAX_LATENCY_COUNT_WIDTH		   = 5; // calibration finds the best latency by reducing the maximum latency
localparam MAX_READ_LATENCY				   = 2**MAX_LATENCY_COUNT_WIDTH; 

// Write Datapath
// The sequencer uses this value to control write latency during calibration
parameter MAX_WRITE_LATENCY_COUNT_WIDTH = 4;
parameter NUM_WRITE_PATH_FLOP_STAGES    = 0;
parameter NUM_WRITE_FR_CYCLE_SHIFTS = 0;

// Add additional FF stage between core and periphery
parameter REGISTER_C2P = "true";

// Address/Command Datapath
parameter NUM_AC_FR_CYCLE_SHIFTS = 1;

// MemCk clock phase select setting
parameter LDC_MEM_CK_CPS_PHASE = 0;


parameter MR1_ODS								= 1;
parameter MR1_RTT								= 2;
parameter MEM_T_WL								= 2;

localparam MEM_T_RL								= 5;

// The sequencer issues back-to-back reads during calibration, NOPs may need to be inserted depending on the burst length
parameter SEQ_BURST_COUNT_WIDTH = 1;

// The DLL offset control width
parameter DLL_OFFSET_CTRL_WIDTH = 6;

parameter MEM_CLK_FREQ = 200.0;
parameter DELAY_BUFFER_MODE = "HIGH";
parameter DQS_DELAY_CHAIN_PHASE_SETTING = 0;
parameter DQS_PHASE_SHIFT = 0;
parameter DELAYED_CLOCK_PHASE_SETTING = 2;
parameter AFI_DEBUG_INFO_WIDTH = 32;

parameter CALIB_REG_WIDTH = 8;


parameter TB_PROTOCOL       = "DDR2";
parameter TB_MEM_CLK_FREQ   = "200.0";
parameter TB_RATE           = "HALF";
parameter TB_MEM_DQ_WIDTH   = "8";
parameter TB_MEM_DQS_WIDTH  = "1";
parameter TB_PLL_DLL_MASTER = "true";

parameter FAST_SIM_CALIBRATION = "false";


parameter EXTRA_VFIFO_SHIFT = 0;

localparam SIM_FILESET = ("false" == "true");


// END PARAMETER SECTION
// ******************************************************************************************************************************** 


// ******************************************************************************************************************************** 
// BEGIN PORT SECTION


// When the PHY is selected to be a PLL/DLL SLAVE, the PLL and DLL are instantied at the top level of the example design
input	pll_mem_clk;	
input pll_mem_phy_clk;
input afi_phy_clk;
input pll_avl_phy_clk;
input	pll_write_clk;
input	pll_write_clk_pre_phy_clk;
input	pll_addr_cmd_clk;
input	pll_avl_clk;
input	pll_config_clk;
input	pll_locked;



input	[DLL_DELAY_CTRL_WIDTH-1:0]  dll_delayctrl;
output  dll_pll_locked;
output  dll_clk;



// Reset Interface, AFI 2.0
input   global_reset_n;		// Resets (active-low) the whole system (all PHY logic + PLL)
input	soft_reset_n;		// Resets (active-low) PHY logic only, PLL is NOT reset
output	afi_reset_n;		// Asynchronously asserted and synchronously de-asserted on afi_clk domain
output	afi_reset_export_n;		// Asynchronously asserted and synchronously de-asserted on afi_clk domain
							// should be used to reset system level afi_clk domain logic
input csr_soft_reset_req;  // Reset request (active_high) being driven by external debug master

// OCT termination control signals
input [OCT_TERM_CONTROL_WIDTH-1:0] parallelterminationcontrol;
input [OCT_TERM_CONTROL_WIDTH-1:0] seriesterminationcontrol;

// PHY-Controller Interface, AFI 2.0
// Control Interface
input   [AFI_ADDR_WIDTH-1:0]        afi_addr;		// address
input   [AFI_BANKADDR_WIDTH-1:0]    afi_ba;			// bank
input   [AFI_CLK_EN_WIDTH-1:0]      afi_cke;		// clock enable
input   [AFI_CS_WIDTH-1:0]          afi_cs_n;		// chip select
input   [AFI_CONTROL_WIDTH-1:0]     afi_ras_n;
input   [AFI_CONTROL_WIDTH-1:0]     afi_we_n;
input   [AFI_CONTROL_WIDTH-1:0]     afi_cas_n;
input   [AFI_ODT_WIDTH-1:0]         afi_odt;


// Write data interface
input   [AFI_WRITE_DQS_WIDTH-1:0]   afi_dqs_burst;
input   [AFI_DQ_WIDTH-1:0]          afi_wdata;			// write data
input	[AFI_WRITE_DQS_WIDTH-1:0]	afi_wdata_valid;	// write data valid, used to maintain write latency required by protocol spec
input   [AFI_DM_WIDTH-1:0]          afi_dm;				// write data mask

// Read data interface
output  [AFI_DQ_WIDTH-1:0]    afi_rdata;           // read data				
input   [AFI_RATE_RATIO-1:0]  afi_rdata_en;        // read enable, used to maintain the read latency calibrated by PHY
input   [AFI_RATE_RATIO-1:0]  afi_rdata_en_full;   // read enable full burst, used to create DQS enable
output  [AFI_RATE_RATIO-1:0]  afi_rdata_valid;     // read data valid

// Status interface
output  afi_cal_success;	// calibration success
output  afi_cal_fail;		// calibration failure

output [AFI_WLAT_WIDTH-1:0]			afi_wlat;
output [AFI_RLAT_WIDTH-1:0]			afi_rlat;
input  [MEM_IF_CK_WIDTH-1:0] afi_mem_clk_disable;

// PHY-Memory Interface

output  [MEM_IF_ADDR_WIDTH-1:0]       mem_a;        // address
output  [MEM_IF_BANKADDR_WIDTH-1:0]   mem_ba;       // bank
output  [MEM_IF_CK_WIDTH-1:0]         mem_ck;       // differential address and command clock
output  [MEM_IF_CK_WIDTH-1:0]         mem_ck_n;
output  [MEM_IF_CLK_EN_WIDTH-1:0]     mem_cke;      // clock enable
output  [MEM_IF_CS_WIDTH-1:0]         mem_cs_n;     // chip select
output  [MEM_IF_DM_WIDTH-1:0]         mem_dm;       // data mask
output  [MEM_IF_CONTROL_WIDTH-1:0]    mem_ras_n;		
output  [MEM_IF_CONTROL_WIDTH-1:0]    mem_cas_n;		
output  [MEM_IF_CONTROL_WIDTH-1:0]    mem_we_n;		
inout	[MEM_IF_DQ_WIDTH-1:0]         mem_dq;       // bidirectional data bus
inout	[MEM_IF_DQS_WIDTH-1:0]        mem_dqs;      // bidirectional data strobe
inout	[MEM_IF_DQS_WIDTH-1:0]        mem_dqs_n;    // differential bidirectional data strobe
output  [MEM_IF_ODT_WIDTH-1:0]        mem_odt;


// PLL Interface
input	afi_clk;
input	afi_half_clk;

wire	pll_dqs_ena_clk;



output  addr_cmd_clk;
output  avl_clk;
output  avl_reset_n;
output  scc_clk;
output  scc_reset_n;

input           [SCC_DATA_WIDTH-1:0]  scc_data;
input    [MEM_IF_READ_DQS_WIDTH-1:0]  scc_dqs_ena;
input    [MEM_IF_READ_DQS_WIDTH-1:0]  scc_dqs_io_ena;
input          [MEM_IF_DQ_WIDTH-1:0]  scc_dq_ena;
input          [MEM_IF_DM_WIDTH-1:0]  scc_dm_ena;
input                          [0:0]  scc_upd;
output   [MEM_IF_READ_DQS_WIDTH-1:0]  capture_strobe_tracking;

output  phy_clk;
output  phy_reset_n;

input  [MAX_LATENCY_COUNT_WIDTH-1:0]  phy_read_latency_counter;
input           [AFI_WLAT_WIDTH-1:0]  phy_afi_wlat;
input           [AFI_RLAT_WIDTH-1:0]  phy_afi_rlat;
input    [MEM_IF_READ_DQS_WIDTH-1:0]  phy_read_increment_vfifo_fr;
input    [MEM_IF_READ_DQS_WIDTH-1:0]  phy_read_increment_vfifo_hr;
input    [MEM_IF_READ_DQS_WIDTH-1:0]  phy_read_increment_vfifo_qr;
input                                 phy_reset_mem_stable;
input   [AFI_DEBUG_INFO_WIDTH - 1:0]  phy_cal_debug_info;
input    [MEM_IF_READ_DQS_WIDTH-1:0]  phy_read_fifo_reset;
input    [MEM_IF_READ_DQS_WIDTH-1:0]  phy_vfifo_rd_en_override;
input                                 phy_cal_success;	// calibration success
input                                 phy_cal_fail;		// calibration failure
output            [AFI_DQ_WIDTH-1:0]  phy_read_fifo_q; 

output         [CALIB_REG_WIDTH-1:0]  calib_skip_steps;


// END PORT SECTION


initial $display("Using %0s core emif simulation models", FAST_SIM_MODEL ? "Fast" : "Regular");


assign afi_cal_success = phy_cal_success;
assign afi_cal_fail = phy_cal_fail;

assign addr_cmd_clk = pll_addr_cmd_clk;
assign avl_clk = pll_avl_clk;
assign scc_clk = pll_config_clk;


integer MEM_T_WL_int = (MEM_T_WL/2);
assign afi_wlat = MEM_T_WL_int[AFI_WLAT_WIDTH-1:0];


// Exporting read latency is currently not supported
assign afi_rlat = 0;


assign pll_dqs_ena_clk = pll_write_clk;

nios_mem_if_ddr2_emif_0_p0_memphy #(
	.DEVICE_FAMILY(DEVICE_FAMILY),
	.OCT_SERIES_TERM_CONTROL_WIDTH(OCT_TERM_CONTROL_WIDTH),
	.OCT_PARALLEL_TERM_CONTROL_WIDTH(OCT_TERM_CONTROL_WIDTH),
	.MEM_ADDRESS_WIDTH(MEM_IF_ADDR_WIDTH),
	.MEM_BANK_WIDTH(MEM_IF_BANKADDR_WIDTH),
	.MEM_CLK_EN_WIDTH(MEM_IF_CLK_EN_WIDTH),
	.MEM_CK_WIDTH(MEM_IF_CK_WIDTH),
	.MEM_ODT_WIDTH(MEM_IF_ODT_WIDTH),
	.MEM_DQS_WIDTH(MEM_IF_DQS_WIDTH),
	.MEM_CHIP_SELECT_WIDTH(MEM_IF_CS_WIDTH),
	.MEM_DM_WIDTH(MEM_IF_DM_WIDTH),
	.MEM_CONTROL_WIDTH(MEM_IF_CONTROL_WIDTH),
	.MEM_DQ_WIDTH(MEM_IF_DQ_WIDTH),
	.MEM_READ_DQS_WIDTH(MEM_IF_READ_DQS_WIDTH),
	.MEM_WRITE_DQS_WIDTH(MEM_IF_WRITE_DQS_WIDTH),
	.AFI_ADDRESS_WIDTH(AFI_ADDR_WIDTH),
	.AFI_BANK_WIDTH(AFI_BANKADDR_WIDTH),
	.AFI_CHIP_SELECT_WIDTH(AFI_CS_WIDTH),
	.AFI_CLK_EN_WIDTH(AFI_CLK_EN_WIDTH),
	.AFI_ODT_WIDTH(AFI_ODT_WIDTH),
	.AFI_MAX_WRITE_LATENCY_COUNT_WIDTH(AFI_WLAT_WIDTH),
	.AFI_MAX_READ_LATENCY_COUNT_WIDTH(AFI_RLAT_WIDTH),
	.AFI_DATA_MASK_WIDTH(AFI_DM_WIDTH),
	.AFI_DQS_WIDTH(AFI_WRITE_DQS_WIDTH),
	.AFI_CONTROL_WIDTH(AFI_CONTROL_WIDTH),
	.AFI_DATA_WIDTH(AFI_DQ_WIDTH),
	.AFI_RATE_RATIO(AFI_RATE_RATIO),
	.DLL_DELAY_CTRL_WIDTH(DLL_DELAY_CTRL_WIDTH),
	.MEM_T_RL(MEM_T_RL),
	.MR1_ODS(MR1_ODS),
	.MR1_RTT(MR1_RTT),
	.MAX_LATENCY_COUNT_WIDTH(MAX_LATENCY_COUNT_WIDTH),
	.MAX_READ_LATENCY(MAX_READ_LATENCY),
	.READ_VALID_FIFO_SIZE(READ_VALID_FIFO_SIZE),
	.READ_FIFO_SIZE(READ_FIFO_SIZE),
	.MAX_WRITE_LATENCY_COUNT_WIDTH(MAX_WRITE_LATENCY_COUNT_WIDTH),
	.NUM_WRITE_PATH_FLOP_STAGES(NUM_WRITE_PATH_FLOP_STAGES),
	.NUM_WRITE_FR_CYCLE_SHIFTS(NUM_WRITE_FR_CYCLE_SHIFTS),
	.REGISTER_C2P(REGISTER_C2P),
	.LDC_MEM_CK_CPS_PHASE(LDC_MEM_CK_CPS_PHASE),
	.NUM_SUBGROUP_PER_READ_DQS(NUM_SUBGROUP_PER_READ_DQS),
	.QVLD_EXTRA_FLOP_STAGES(QVLD_EXTRA_FLOP_STAGES),
	.QVLD_WR_ADDRESS_OFFSET(QVLD_WR_ADDRESS_OFFSET),
	.NUM_AC_FR_CYCLE_SHIFTS(NUM_AC_FR_CYCLE_SHIFTS),
	.ALTDQDQS_INPUT_FREQ(MEM_CLK_FREQ),
	.ALTDQDQS_DELAY_CHAIN_BUFFER_MODE(DELAY_BUFFER_MODE),
	.ALTDQDQS_DQS_PHASE_SETTING(DQS_DELAY_CHAIN_PHASE_SETTING),
	.ALTDQDQS_DQS_PHASE_SHIFT(DQS_PHASE_SHIFT),
	.ALTDQDQS_DELAYED_CLOCK_PHASE_SETTING(DELAYED_CLOCK_PHASE_SETTING),
	.CALIB_REG_WIDTH(CALIB_REG_WIDTH),
	.AFI_DEBUG_INFO_WIDTH(AFI_DEBUG_INFO_WIDTH),
	.TB_PROTOCOL(TB_PROTOCOL),
	.TB_MEM_CLK_FREQ(TB_MEM_CLK_FREQ),
	.TB_RATE(TB_RATE),
	.TB_MEM_DQ_WIDTH(TB_MEM_DQ_WIDTH),
	.TB_MEM_DQS_WIDTH(TB_MEM_DQS_WIDTH),
	.TB_PLL_DLL_MASTER(TB_PLL_DLL_MASTER),
	.EXTRA_VFIFO_SHIFT(EXTRA_VFIFO_SHIFT),
	.FAST_SIM_MODEL(FAST_SIM_MODEL),
	.FAST_SIM_CALIBRATION(FAST_SIM_CALIBRATION),
	.SCC_DATA_WIDTH(SCC_DATA_WIDTH)
) umemphy (
	.global_reset_n(global_reset_n),
	.soft_reset_n(soft_reset_n & ~csr_soft_reset_req),
	.ctl_reset_n(afi_reset_n),
	.ctl_reset_export_n(afi_reset_export_n),
	.pll_locked(pll_locked),
	.oct_ctl_rt_value(parallelterminationcontrol),
	.oct_ctl_rs_value(seriesterminationcontrol),
	.afi_addr(afi_addr),
	.afi_ba(afi_ba),
	.afi_cke(afi_cke),
	.afi_cs_n(afi_cs_n),
	.afi_ras_n(afi_ras_n),
	.afi_we_n(afi_we_n),
	.afi_cas_n(afi_cas_n),
	.afi_odt(afi_odt),
	.afi_dqs_burst(afi_dqs_burst),
	.afi_wdata(afi_wdata),
	.afi_wdata_valid(afi_wdata_valid),
	.afi_dm(afi_dm),
	.afi_rdata(afi_rdata),
	.afi_rdata_en(afi_rdata_en),
	.afi_rdata_en_full(afi_rdata_en_full),
	.afi_rdata_valid(afi_rdata_valid),
	.afi_mem_clk_disable(afi_mem_clk_disable),
	.afi_cal_success(afi_cal_success),
	.afi_cal_fail(afi_cal_fail),
	.mem_a(mem_a),
	.mem_ba(mem_ba),
	.mem_ck(mem_ck),
	.mem_ck_n(mem_ck_n),
	.mem_cke(mem_cke),
	.mem_cs_n(mem_cs_n),
	.mem_dm(mem_dm),
	.mem_ras_n(mem_ras_n),
	.mem_cas_n(mem_cas_n),
	.mem_we_n(mem_we_n),
	.mem_dq(mem_dq),
	.mem_dqs(mem_dqs),
	.mem_dqs_n(mem_dqs_n),
	.mem_odt(mem_odt),
	.pll_afi_clk(afi_clk),
	.pll_mem_clk(pll_mem_clk),
	.pll_mem_phy_clk(pll_mem_phy_clk),
	.pll_afi_phy_clk(afi_phy_clk),
	.pll_write_clk(pll_write_clk),
	.pll_write_clk_pre_phy_clk(pll_write_clk_pre_phy_clk),
	.pll_addr_cmd_clk(pll_addr_cmd_clk),
	.pll_afi_half_clk(afi_half_clk),
	.pll_dqs_ena_clk(pll_dqs_ena_clk),
	.seq_clk(afi_clk), 
	.reset_n_avl_clk(avl_reset_n),
	.reset_n_scc_clk(scc_reset_n),
	.scc_data(scc_data),
	.scc_dqs_ena(scc_dqs_ena),
	.scc_dqs_io_ena(scc_dqs_io_ena),
	.scc_dq_ena(scc_dq_ena),
	.scc_dm_ena(scc_dm_ena),
	.scc_upd(scc_upd),
	.capture_strobe_tracking(capture_strobe_tracking),
	.phy_clk(phy_clk),
	.phy_reset_n(phy_reset_n),
	.phy_read_latency_counter(phy_read_latency_counter),
	.phy_afi_wlat(phy_afi_wlat),
	.phy_afi_rlat(phy_afi_rlat),
	.phy_read_increment_vfifo_fr(phy_read_increment_vfifo_fr),
	.phy_read_increment_vfifo_hr(phy_read_increment_vfifo_hr),
	.phy_read_increment_vfifo_qr(phy_read_increment_vfifo_qr),
	.phy_reset_mem_stable(phy_reset_mem_stable),
	.phy_cal_debug_info(phy_cal_debug_info),
	.phy_read_fifo_reset(phy_read_fifo_reset),
	.phy_vfifo_rd_en_override(phy_vfifo_rd_en_override),
	.phy_read_fifo_q(phy_read_fifo_q),
	.calib_skip_steps(calib_skip_steps),
	.pll_avl_clk(pll_avl_clk),
	.pll_config_clk(pll_config_clk),
	.dll_clk(dll_clk),
    .dll_pll_locked(dll_pll_locked),
	.dll_phy_delayctrl(dll_delayctrl)
);

endmodule

