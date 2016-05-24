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
// File name: memphy.v
// This file instantiates all the main components of the PHY. 
// ******************************************************************************************************************************** 

`timescale 1 ps / 1 ps

module nios_mem_if_ddr2_emif_0_p0_memphy(
	global_reset_n,
	soft_reset_n,
	ctl_reset_n,
	ctl_reset_export_n,
	pll_locked,
	oct_ctl_rs_value,
	oct_ctl_rt_value,
	afi_addr,
	afi_cke,
	afi_cs_n,
	afi_ba,
	afi_cas_n,
	afi_odt,
	afi_ras_n,
	afi_we_n,
	afi_mem_clk_disable,
	afi_dqs_burst,
	afi_wlat,
	afi_rlat,
	afi_wdata,
	afi_wdata_valid,
	afi_dm,
	afi_rdata,
	afi_rdata_en,
	afi_rdata_en_full,
	afi_rdata_valid,
	afi_cal_debug_info,
	afi_cal_success,
	afi_cal_fail,
	mem_a,
	mem_ba,
	mem_ck,
	mem_ck_n,
	mem_cke,
	mem_cs_n,
	mem_dm,
	mem_odt,
	mem_ras_n,
	mem_cas_n,
	mem_we_n,
	mem_dq,
	mem_dqs,
	mem_dqs_n,
	reset_n_scc_clk,
	reset_n_avl_clk,
	scc_data,
	scc_dqs_ena,
	scc_dqs_io_ena,
	scc_dq_ena,
	scc_dm_ena,
	scc_upd,
	scc_sr_dqsenable_delayctrl,
	scc_sr_dqsdisablen_delayctrl,
	scc_sr_multirank_delayctrl,
	capture_strobe_tracking,
	phy_clk,
	phy_reset_n,
	phy_read_latency_counter,
	phy_afi_wlat,
	phy_afi_rlat,
	phy_num_write_fr_cycle_shifts,
	phy_read_increment_vfifo_fr,
	phy_read_increment_vfifo_hr,
	phy_read_increment_vfifo_qr,
	phy_reset_mem_stable,
	phy_cal_debug_info,
	phy_read_fifo_reset,
	phy_vfifo_rd_en_override,
	phy_read_fifo_q,
	calib_skip_steps,
	pll_afi_clk,
	pll_afi_half_clk,
	pll_addr_cmd_clk,
	pll_mem_clk,
	pll_mem_phy_clk,
	pll_afi_phy_clk,
	pll_write_clk,
	pll_write_clk_pre_phy_clk,
	pll_dqs_ena_clk,
	seq_clk,
	pll_avl_clk,
	pll_config_clk,
	dll_clk,
    dll_pll_locked,
	dll_phy_delayctrl
);

// ******************************************************************************************************************************** 
// BEGIN PARAMETER SECTION
// All parameters default to "" will have their values passed in from higher level wrapper with the controller and driver 
parameter DEVICE_FAMILY = "";

// On-chip termination
parameter OCT_SERIES_TERM_CONTROL_WIDTH   = ""; 
parameter OCT_PARALLEL_TERM_CONTROL_WIDTH = ""; 

// PHY-Memory Interface
// Memory device specific parameters, they are set according to the memory spec
parameter MEM_ADDRESS_WIDTH     = ""; 
parameter MEM_BANK_WIDTH        = "";
parameter MEM_CLK_EN_WIDTH 	    = ""; 
parameter MEM_CK_WIDTH 	    	= ""; 
parameter MEM_ODT_WIDTH 		= ""; 
parameter MEM_DQS_WIDTH         = "";
parameter MEM_CHIP_SELECT_WIDTH = ""; 
parameter MEM_CONTROL_WIDTH     = ""; 
parameter MEM_DM_WIDTH          = ""; 
parameter MEM_DQ_WIDTH          = ""; 
parameter MEM_READ_DQS_WIDTH    = ""; 
parameter MEM_WRITE_DQS_WIDTH   = "";
parameter MEM_IF_NUMBER_OF_RANKS	= "";

// PHY-Controller (AFI) Interface
// The AFI interface widths are derived from the memory interface widths based on full/half rate operations
// The calculations are done on higher level wrapper
parameter AFI_ADDRESS_WIDTH         = ""; 
parameter AFI_DEBUG_INFO_WIDTH = "";
parameter AFI_BANK_WIDTH            = "";
parameter AFI_CHIP_SELECT_WIDTH     = "";
parameter AFI_CLK_EN_WIDTH     		= "";
parameter AFI_ODT_WIDTH     		= "";
parameter AFI_MAX_WRITE_LATENCY_COUNT_WIDTH = "";
parameter AFI_MAX_READ_LATENCY_COUNT_WIDTH = "";
parameter AFI_DATA_MASK_WIDTH       = "";
parameter AFI_CONTROL_WIDTH         = "";
parameter AFI_DATA_WIDTH            = "";
parameter AFI_DQS_WIDTH             = "";
parameter AFI_RATE_RATIO            = "";
parameter AFI_RRANK_WIDTH	    = "";
parameter AFI_WRANK_WIDTH	    = "";

// DLL Interface
// The DLL delay output control is always 6 bits for current existing devices
parameter DLL_DELAY_CTRL_WIDTH  = "";

parameter SCC_DATA_WIDTH = "";

// Read Datapath parameters for timing purposes
parameter NUM_SUBGROUP_PER_READ_DQS        = "";
parameter QVLD_EXTRA_FLOP_STAGES		   = "";
parameter QVLD_WR_ADDRESS_OFFSET		   = "";

// Read Datapath parameters, the values should not be changed unless the intention is to change the architecture
parameter READ_VALID_FIFO_SIZE             = "";
parameter READ_FIFO_SIZE                   = "";

// Latency calibration parameters
parameter MAX_LATENCY_COUNT_WIDTH          = "";
parameter MAX_READ_LATENCY                 = "";

// Write Datapath
// The sequencer uses this value to control write latency during calibration
parameter MAX_WRITE_LATENCY_COUNT_WIDTH = "";
parameter NUM_WRITE_PATH_FLOP_STAGES	= "";
parameter NUM_WRITE_FR_CYCLE_SHIFTS		= "";

// Add register stage between core and periphery for C2P transfers
parameter REGISTER_C2P = "";

// MemCK clock phase select setting
parameter LDC_MEM_CK_CPS_PHASE = "";

// Address/Command Datapath
parameter NUM_AC_FR_CYCLE_SHIFTS = "";

parameter MEM_T_RL              = "";

parameter MR1_ODS				= "";
parameter MR1_RTT				= "";

parameter ALTDQDQS_INPUT_FREQ = "";
parameter ALTDQDQS_DELAY_CHAIN_BUFFER_MODE = "";
parameter ALTDQDQS_DQS_PHASE_SETTING = "";
parameter ALTDQDQS_DQS_PHASE_SHIFT = "";
parameter ALTDQDQS_DELAYED_CLOCK_PHASE_SETTING = "";


parameter EXTRA_VFIFO_SHIFT = 0;

parameter TB_PROTOCOL        = "";
parameter TB_MEM_CLK_FREQ    = "";
parameter TB_RATE            = "";
parameter TB_MEM_DQ_WIDTH    = "";
parameter TB_MEM_DQS_WIDTH   = "";
parameter TB_PLL_DLL_MASTER  = "";

parameter FAST_SIM_MODEL = "";
parameter FAST_SIM_CALIBRATION = "";
 
// Local parameters
localparam DOUBLE_MEM_DQ_WIDTH = MEM_DQ_WIDTH * 2;
localparam HALF_AFI_DATA_WIDTH = AFI_DATA_WIDTH / 2;

// Width of the calibration status register used to control calibration skipping.
parameter CALIB_REG_WIDTH = "";

// The number of AFI Resets to generate
localparam NUM_AFI_RESET = 4;

// Read valid predication parameters
localparam READ_VALID_FIFO_WRITE_MEM_DEPTH	= READ_VALID_FIFO_SIZE / 2; // write operates on half rate clock
localparam READ_VALID_FIFO_READ_MEM_DEPTH	= READ_VALID_FIFO_SIZE; // valid-read-prediction operates on full rate clock
localparam READ_VALID_FIFO_PER_DQS_WIDTH	= 1; // valid fifo output is a full-rate signal
localparam READ_VALID_FIFO_WIDTH		= READ_VALID_FIFO_PER_DQS_WIDTH * MEM_READ_DQS_WIDTH;
localparam READ_VALID_FIFO_WRITE_ADDR_WIDTH	= ceil_log2(READ_VALID_FIFO_WRITE_MEM_DEPTH);
localparam READ_VALID_FIFO_READ_ADDR_WIDTH	= ceil_log2(READ_VALID_FIFO_READ_MEM_DEPTH);

// Data resynchronization FIFO
localparam READ_FIFO_WRITE_MEM_DEPTH		= READ_FIFO_SIZE / 2; // data is written on half rate clock
localparam READ_FIFO_READ_MEM_DEPTH			= READ_FIFO_SIZE / 2; // data is read out on half rate clock
localparam READ_FIFO_WRITE_ADDR_WIDTH		= ceil_log2(READ_FIFO_WRITE_MEM_DEPTH);
localparam READ_FIFO_READ_ADDR_WIDTH		= ceil_log2(READ_FIFO_READ_MEM_DEPTH);

// Sequencer parameters
localparam SEQ_ADDRESS_WIDTH		= AFI_ADDRESS_WIDTH;
localparam SEQ_BANK_WIDTH			= AFI_BANK_WIDTH;
localparam SEQ_CHIP_SELECT_WIDTH	= AFI_CHIP_SELECT_WIDTH;
localparam SEQ_CLK_EN_WIDTH			= AFI_CLK_EN_WIDTH;
localparam SEQ_ODT_WIDTH			= AFI_ODT_WIDTH;
localparam SEQ_DATA_MASK_WIDTH		= AFI_DATA_MASK_WIDTH;
localparam SEQ_CONTROL_WIDTH		= AFI_CONTROL_WIDTH;
localparam SEQ_DATA_WIDTH			= AFI_DATA_WIDTH;
localparam SEQ_DQS_WIDTH			= AFI_DQS_WIDTH;

localparam MAX_LATENCY_COUNT_WIDTH_SAFE    = (MAX_LATENCY_COUNT_WIDTH < 5)? 5 :  MAX_LATENCY_COUNT_WIDTH;

// END PARAMETER SECTION
// ******************************************************************************************************************************** 



// ******************************************************************************************************************************** 
// BEGIN PORT SECTION

//  Reset Interface
input	global_reset_n;		// Resets (active-low) the whole system (all PHY logic + PLL)
input	soft_reset_n;		// Resets (active-low) PHY logic only, PLL is NOT reset
input	pll_locked;			// Indicates that PLL is locked
output	ctl_reset_export_n;	// Asynchronously asserted and synchronously de-asserted on afi_clk domain
output	ctl_reset_n;		// Asynchronously asserted and synchronously de-asserted on afi_clk domain


input   [OCT_SERIES_TERM_CONTROL_WIDTH-1:0] oct_ctl_rs_value;
input   [OCT_PARALLEL_TERM_CONTROL_WIDTH-1:0] oct_ctl_rt_value;

// PHY-Controller Interface, AFI 2.0
// Control Interface
input   [AFI_ADDRESS_WIDTH-1:0] afi_addr;       // address




input   [AFI_CLK_EN_WIDTH-1:0] afi_cke;
input   [AFI_CHIP_SELECT_WIDTH-1:0] afi_cs_n;
input   [AFI_BANK_WIDTH-1:0]    afi_ba;
input   [AFI_CONTROL_WIDTH-1:0] afi_cas_n;
input   [AFI_ODT_WIDTH-1:0] afi_odt;
input   [AFI_CONTROL_WIDTH-1:0] afi_ras_n;
input   [AFI_CONTROL_WIDTH-1:0] afi_we_n;
input   [MEM_CK_WIDTH-1:0] afi_mem_clk_disable;
input   [AFI_DQS_WIDTH-1:0]	afi_dqs_burst;	
output  [AFI_MAX_WRITE_LATENCY_COUNT_WIDTH-1:0] afi_wlat;
output  [AFI_MAX_READ_LATENCY_COUNT_WIDTH-1:0]  afi_rlat;

// Write data interface
input   [AFI_DATA_WIDTH-1:0]    afi_wdata;              // write data
input   [AFI_DQS_WIDTH-1:0]		afi_wdata_valid;    	// write data valid, used to maintain write latency required by protocol spec
input   [AFI_DATA_MASK_WIDTH-1:0]   afi_dm;             // write data mask

// Read data interface
output  [AFI_DATA_WIDTH-1:0]    afi_rdata;              // read data                
input   [AFI_RATE_RATIO-1:0]    afi_rdata_en;           // read enable, used to maintain the read latency calibrated by PHY
input   [AFI_RATE_RATIO-1:0]    afi_rdata_en_full;      // read enable full burst, used to create DQS enable
output  [AFI_RATE_RATIO-1:0]    afi_rdata_valid;        // read data valid

// Status interface
input  afi_cal_success;    // calibration success
input  afi_cal_fail;       // calibration failure
output [AFI_DEBUG_INFO_WIDTH - 1:0] afi_cal_debug_info;

// PHY-Memory Interface



output  [MEM_ADDRESS_WIDTH-1:0] mem_a;
output  [MEM_BANK_WIDTH-1:0]    mem_ba;
output  [MEM_CK_WIDTH-1:0]	mem_ck;
output  [MEM_CK_WIDTH-1:0]	mem_ck_n;
output  [MEM_CLK_EN_WIDTH-1:0] mem_cke;
output  [MEM_CHIP_SELECT_WIDTH-1:0] mem_cs_n;
output  [MEM_DM_WIDTH-1:0]  mem_dm;
output  [MEM_ODT_WIDTH-1:0] mem_odt;
output  [MEM_CONTROL_WIDTH-1:0] mem_ras_n;
output  [MEM_CONTROL_WIDTH-1:0] mem_cas_n;
output  [MEM_CONTROL_WIDTH-1:0] mem_we_n;
inout   [MEM_DQ_WIDTH-1:0]  mem_dq;
inout   [MEM_DQS_WIDTH-1:0] mem_dqs;
inout   [MEM_DQS_WIDTH-1:0] mem_dqs_n;



output	reset_n_scc_clk;
output	reset_n_avl_clk;
input        [SCC_DATA_WIDTH-1:0]  scc_data;
input    [MEM_READ_DQS_WIDTH-1:0]  scc_dqs_ena;
input    [MEM_READ_DQS_WIDTH-1:0]  scc_dqs_io_ena;
input          [MEM_DQ_WIDTH-1:0]  scc_dq_ena;
input          [MEM_DM_WIDTH-1:0]  scc_dm_ena;
input    [0:0]                     scc_upd;
input    [7:0]                     scc_sr_dqsenable_delayctrl;
input    [7:0]                     scc_sr_dqsdisablen_delayctrl;
input    [7:0]                     scc_sr_multirank_delayctrl;
output   [MEM_READ_DQS_WIDTH-1:0]  capture_strobe_tracking;

output  phy_clk;
output  phy_reset_n;

input  [MAX_LATENCY_COUNT_WIDTH-1:0]  phy_read_latency_counter;
input  [AFI_MAX_WRITE_LATENCY_COUNT_WIDTH-1:0]  phy_afi_wlat;
input  [AFI_MAX_READ_LATENCY_COUNT_WIDTH-1:0]  phy_afi_rlat;
input  [MEM_WRITE_DQS_WIDTH*2-1:0]  phy_num_write_fr_cycle_shifts;
input  [MEM_READ_DQS_WIDTH-1:0]     phy_read_increment_vfifo_fr;
input  [MEM_READ_DQS_WIDTH-1:0]     phy_read_increment_vfifo_hr;
input  [MEM_READ_DQS_WIDTH-1:0]     phy_read_increment_vfifo_qr;
input                               phy_reset_mem_stable;
input   [AFI_DEBUG_INFO_WIDTH - 1:0]  phy_cal_debug_info;
input       [MEM_READ_DQS_WIDTH-1:0]  phy_read_fifo_reset;
input       [MEM_READ_DQS_WIDTH-1:0]  phy_vfifo_rd_en_override;
output          [AFI_DATA_WIDTH-1:0]  phy_read_fifo_q;

output         [CALIB_REG_WIDTH-1:0]  calib_skip_steps;


// PLL Interface
input	pll_afi_clk;		// clocks AFI interface logic
input	pll_afi_half_clk;	// 
input	pll_addr_cmd_clk;	// clocks address/command DDIO
input	pll_mem_clk;		// output clock to memory
input	pll_write_clk;		// clocks write data DDIO
input	pll_write_clk_pre_phy_clk;
input pll_mem_phy_clk;
input pll_afi_phy_clk;
input	pll_dqs_ena_clk;
input	seq_clk;
input	pll_avl_clk;
input	pll_config_clk;


// DLL Interface
output  dll_clk;
output  dll_pll_locked;
input   [DLL_DELAY_CTRL_WIDTH-1:0]  dll_phy_delayctrl;   // dll output used to control the input DQS phase shift




// END PARAMETER SECTION
// ******************************************************************************************************************************** 

wire	[AFI_ADDRESS_WIDTH-1:0]	phy_ddio_address;
wire	[AFI_BANK_WIDTH-1:0]    phy_ddio_bank;
wire	[AFI_CHIP_SELECT_WIDTH-1:0] phy_ddio_cs_n;
wire	[AFI_CLK_EN_WIDTH-1:0] phy_ddio_cke;
wire	[AFI_ODT_WIDTH-1:0] phy_ddio_odt;
wire	[AFI_CONTROL_WIDTH-1:0] phy_ddio_ras_n;
wire	[AFI_CONTROL_WIDTH-1:0] phy_ddio_cas_n;
wire	[AFI_CONTROL_WIDTH-1:0]	phy_ddio_we_n;
wire	[AFI_DATA_WIDTH-1:0]  phy_ddio_dq;
wire	[AFI_DQS_WIDTH-1:0]  phy_ddio_dqs_en;
wire	[AFI_DQS_WIDTH-1:0]  phy_ddio_oct_ena;
wire	[AFI_DQS_WIDTH-1:0]  phy_ddio_wrdata_en;
wire	[AFI_DATA_MASK_WIDTH-1:0] phy_ddio_wrdata_mask;
localparam DDIO_PHY_DQ_WIDTH = AFI_DATA_WIDTH;
wire	[DDIO_PHY_DQ_WIDTH-1:0] ddio_phy_dq;
wire	[MEM_READ_DQS_WIDTH-1:0] read_capture_clk;


wire	[AFI_DATA_WIDTH-1:0]    afi_rdata;
wire  	[AFI_RATE_RATIO-1:0]    afi_rdata_valid;


wire	[SEQ_ADDRESS_WIDTH-1:0] seq_mux_address;
wire	[SEQ_BANK_WIDTH-1:0]    seq_mux_bank;
wire	[SEQ_CHIP_SELECT_WIDTH-1:0] seq_mux_cs_n;
wire	[SEQ_CLK_EN_WIDTH-1:0] seq_mux_cke;
wire	[SEQ_ODT_WIDTH-1:0] seq_mux_odt;
wire	[SEQ_CONTROL_WIDTH-1:0] seq_mux_ras_n;
wire	[SEQ_CONTROL_WIDTH-1:0] seq_mux_cas_n;
wire	[SEQ_CONTROL_WIDTH-1:0] seq_mux_we_n;
wire	[SEQ_DQS_WIDTH-1:0]	seq_mux_dqs_en;
wire	[SEQ_DATA_WIDTH-1:0]    seq_mux_wdata;
wire	[SEQ_DQS_WIDTH-1:0]	seq_mux_wdata_valid;
wire	[SEQ_DATA_MASK_WIDTH-1:0]   seq_mux_dm;
wire	seq_mux_rdata_en;
wire	[SEQ_DATA_WIDTH-1:0]    mux_seq_rdata;
wire	mux_seq_rdata_valid;    
wire	mux_sel;

wire	[NUM_AFI_RESET-1:0] reset_n_afi_clk;
wire	reset_n_addr_cmd_clk;
wire	reset_n_seq_clk;
wire	reset_n_resync_clk;
wire	[READ_VALID_FIFO_WIDTH-1:0] dqs_enable_ctrl;

wire	[AFI_DQS_WIDTH-1:0] force_oct_off;

wire	reset_n_scc_clk;
wire	reset_n_avl_clk;


wire [MEM_CK_WIDTH-1:0] afi_mem_clk_disable;

localparam SKIP_CALIBRATION_STEPS = 7'b1111111;

localparam CALIBRATION_STEPS = SKIP_CALIBRATION_STEPS;

localparam SKIP_MEM_INIT = 1'b1;

localparam SEQ_CALIB_INIT = {CALIBRATION_STEPS, SKIP_MEM_INIT};

reg [CALIB_REG_WIDTH-1:0] seq_calib_init_reg /* synthesis syn_noprune syn_preserve = 1 */;

// Initialization of the sequencer status register. This register
// is preserved in the netlist so that it can be forced during simulation
always @(posedge pll_afi_clk)
	`ifndef SYNTH_FOR_SIM
	//synthesis translate_off
	`endif
	seq_calib_init_reg <= SEQ_CALIB_INIT;
	`ifndef SYNTH_FOR_SIM
	//synthesis translate_on
	//synthesis read_comments_as_HDL on
	`endif
	// seq_calib_init_reg <= {CALIB_REG_WIDTH{1'b0}};
	`ifndef SYNTH_FOR_SIM
	// synthesis read_comments_as_HDL off
	`endif

// ******************************************************************************************************************************** 
// The reset scheme used in the UNIPHY is asynchronous assert and synchronous de-assert
// The reset block has 2 main functionalities:
// 1. Keep all the PHY logic in reset state until after the PLL is locked
// 2. Synchronize the reset to each clock domain 
// ******************************************************************************************************************************** 


	nios_mem_if_ddr2_emif_0_p0_reset	ureset(
		.pll_afi_clk				(pll_afi_clk),
		.pll_addr_cmd_clk			(pll_addr_cmd_clk),
		.pll_dqs_ena_clk			(pll_dqs_ena_clk),
		.seq_clk					(seq_clk),
		.pll_avl_clk				(pll_avl_clk),
		.scc_clk					(pll_config_clk),
		.reset_n_scc_clk			(reset_n_scc_clk),
		.reset_n_avl_clk			(reset_n_avl_clk),
		.read_capture_clk			(read_capture_clk),
		.pll_locked					(pll_locked),
		.global_reset_n				(global_reset_n),
		.soft_reset_n				(soft_reset_n),
		.ctl_reset_export_n			(ctl_reset_export_n),
		.ctl_reset_n				(ctl_reset_n),
		.reset_n_afi_clk			(reset_n_afi_clk),
		.reset_n_addr_cmd_clk		(reset_n_addr_cmd_clk),
		.reset_n_seq_clk			(reset_n_seq_clk),
		.reset_n_resync_clk			(reset_n_resync_clk),
		.seq_reset_mem_stable       (),
		.reset_n_read_capture_clk	()
	);

	defparam ureset.MEM_READ_DQS_WIDTH = MEM_READ_DQS_WIDTH;
	defparam ureset.NUM_AFI_RESET = NUM_AFI_RESET;




assign calib_skip_steps = seq_calib_init_reg;
assign afi_cal_debug_info = phy_cal_debug_info;

assign phy_clk = seq_clk;
assign phy_reset_n = reset_n_seq_clk;


assign dll_clk = pll_write_clk_pre_phy_clk;

assign dll_pll_locked = pll_locked;


	assign afi_wlat = phy_afi_wlat;
	assign afi_rlat = phy_afi_rlat;



// ******************************************************************************************************************************** 
// The address and command datapath is responsible for adding any flop stages/extra logic that may be required between the AFI
// interface and the output DDIOs.
// ******************************************************************************************************************************** 

    nios_mem_if_ddr2_emif_0_p0_addr_cmd_datapath	uaddr_cmd_datapath(
		.clk		(pll_addr_cmd_clk),
		.reset_n	    		(reset_n_afi_clk[1]), 
		.afi_address	    	(afi_addr),
        .afi_bank               (afi_ba),
        .afi_cs_n               (afi_cs_n),
        .afi_cke                (afi_cke),
        .afi_odt                (afi_odt),
        .afi_ras_n              (afi_ras_n),
        .afi_cas_n              (afi_cas_n),
        .afi_we_n               (afi_we_n),
		.phy_ddio_address		(phy_ddio_address),
	    .phy_ddio_odt    			(phy_ddio_odt),
		.phy_ddio_bank 		   	(phy_ddio_bank),
		.phy_ddio_cs_n    		(phy_ddio_cs_n),
		.phy_ddio_cke    		(phy_ddio_cke),
		.phy_ddio_we_n    		(phy_ddio_we_n),
		.phy_ddio_ras_n   		(phy_ddio_ras_n),
		.phy_ddio_cas_n   		(phy_ddio_cas_n)
    );
        defparam uaddr_cmd_datapath.MEM_ADDRESS_WIDTH                  = MEM_ADDRESS_WIDTH;
        defparam uaddr_cmd_datapath.MEM_BANK_WIDTH                     = MEM_BANK_WIDTH;
        defparam uaddr_cmd_datapath.MEM_CHIP_SELECT_WIDTH              = MEM_CHIP_SELECT_WIDTH;
        defparam uaddr_cmd_datapath.MEM_CLK_EN_WIDTH              	   = MEM_CLK_EN_WIDTH;
        defparam uaddr_cmd_datapath.MEM_ODT_WIDTH              		   = MEM_ODT_WIDTH;
        defparam uaddr_cmd_datapath.MEM_DM_WIDTH                       = MEM_DM_WIDTH;
        defparam uaddr_cmd_datapath.MEM_CONTROL_WIDTH                  = MEM_CONTROL_WIDTH;
        defparam uaddr_cmd_datapath.MEM_DQ_WIDTH                       = MEM_DQ_WIDTH;
        defparam uaddr_cmd_datapath.MEM_READ_DQS_WIDTH                 = MEM_READ_DQS_WIDTH;
        defparam uaddr_cmd_datapath.MEM_WRITE_DQS_WIDTH                = MEM_WRITE_DQS_WIDTH;
        defparam uaddr_cmd_datapath.AFI_ADDRESS_WIDTH                  = AFI_ADDRESS_WIDTH;
        defparam uaddr_cmd_datapath.AFI_BANK_WIDTH                     = AFI_BANK_WIDTH;
        defparam uaddr_cmd_datapath.AFI_CHIP_SELECT_WIDTH              = AFI_CHIP_SELECT_WIDTH;
        defparam uaddr_cmd_datapath.AFI_CLK_EN_WIDTH              	   = AFI_CLK_EN_WIDTH;
        defparam uaddr_cmd_datapath.AFI_ODT_WIDTH              		   = AFI_ODT_WIDTH;
        defparam uaddr_cmd_datapath.AFI_DATA_MASK_WIDTH                = AFI_DATA_MASK_WIDTH;
        defparam uaddr_cmd_datapath.AFI_CONTROL_WIDTH                  = AFI_CONTROL_WIDTH;
        defparam uaddr_cmd_datapath.AFI_DATA_WIDTH                     = AFI_DATA_WIDTH;
        defparam uaddr_cmd_datapath.NUM_AC_FR_CYCLE_SHIFTS             = NUM_AC_FR_CYCLE_SHIFTS;    



// ******************************************************************************************************************************** 
// The write datapath is responsible for adding any flop stages/extra logic that may be required between the AFI interface 
// and the output DDIOs.
// ******************************************************************************************************************************** 

    nios_mem_if_ddr2_emif_0_p0_write_datapath	uwrite_datapath(
		.pll_afi_clk                   (pll_afi_clk),
		.reset_n                       (reset_n_afi_clk[2]),
		.force_oct_off                 (force_oct_off),
		.phy_ddio_oct_ena              (phy_ddio_oct_ena),
		.afi_dqs_en                    (afi_dqs_burst),
		.afi_wdata                     (afi_wdata),
		.afi_wdata_valid               (afi_wdata_valid),
		.afi_dm                        (afi_dm),
		.phy_ddio_dq                   (phy_ddio_dq),
		.phy_ddio_dqs_en               (phy_ddio_dqs_en),
		.phy_ddio_wrdata_en            (phy_ddio_wrdata_en),
		.phy_ddio_wrdata_mask          (phy_ddio_wrdata_mask),
		.seq_num_write_fr_cycle_shifts (phy_num_write_fr_cycle_shifts)
    );
        defparam uwrite_datapath.MEM_ADDRESS_WIDTH                  = MEM_ADDRESS_WIDTH;
        defparam uwrite_datapath.MEM_DM_WIDTH                       = MEM_DM_WIDTH;
        defparam uwrite_datapath.MEM_CONTROL_WIDTH                  = MEM_CONTROL_WIDTH;
        defparam uwrite_datapath.MEM_DQ_WIDTH                       = MEM_DQ_WIDTH;
        defparam uwrite_datapath.MEM_READ_DQS_WIDTH                 = MEM_READ_DQS_WIDTH;
        defparam uwrite_datapath.MEM_WRITE_DQS_WIDTH                = MEM_WRITE_DQS_WIDTH;
        defparam uwrite_datapath.AFI_ADDRESS_WIDTH                  = AFI_ADDRESS_WIDTH;
        defparam uwrite_datapath.AFI_DATA_MASK_WIDTH                = AFI_DATA_MASK_WIDTH;
        defparam uwrite_datapath.AFI_CONTROL_WIDTH                  = AFI_CONTROL_WIDTH;
        defparam uwrite_datapath.AFI_DATA_WIDTH                     = AFI_DATA_WIDTH;
        defparam uwrite_datapath.AFI_DQS_WIDTH                      = AFI_DQS_WIDTH;
        defparam uwrite_datapath.NUM_WRITE_PATH_FLOP_STAGES         = NUM_WRITE_PATH_FLOP_STAGES;
        defparam uwrite_datapath.NUM_WRITE_FR_CYCLE_SHIFTS          = NUM_WRITE_FR_CYCLE_SHIFTS;

	 
// ******************************************************************************************************************************** 
// The read datapath is responsible for read data resynchronization from the memory clock domain to the AFI clock domain.
// It contains 1 FIFO per DQS group for read valid prediction and 1 FIFO per DQS group for read data synchronization.
// ******************************************************************************************************************************** 
	
	nios_mem_if_ddr2_emif_0_p0_read_datapath	uread_datapath(
		.reset_n_afi_clk				(reset_n_afi_clk[3]),
        .reset_n_resync_clk         	(reset_n_resync_clk),
		.seq_read_fifo_reset			(phy_read_fifo_reset),
		.pll_afi_clk					(pll_afi_clk),
		.pll_dqs_ena_clk				(pll_dqs_ena_clk),
		.read_capture_clk				(read_capture_clk),
		.ddio_phy_dq					(ddio_phy_dq),
		.seq_read_latency_counter		({{(MAX_LATENCY_COUNT_WIDTH_SAFE - 5){1'b0}}, phy_read_latency_counter[4:0]}),
		.seq_read_increment_vfifo_fr	(phy_read_increment_vfifo_fr),
		.seq_read_increment_vfifo_hr	(phy_read_increment_vfifo_hr),
		.seq_read_increment_vfifo_qr	(phy_read_increment_vfifo_qr),
		.force_oct_off					(force_oct_off),
		.dqs_enable_ctrl				(dqs_enable_ctrl),
		.afi_rdata_en					(afi_rdata_en),
		.afi_rdata_en_full				(afi_rdata_en_full),
		.afi_rdata						(afi_rdata),
		.phy_mux_read_fifo_q            (phy_read_fifo_q),
		.afi_rdata_valid				(afi_rdata_valid),
		.seq_calib_init					(seq_calib_init_reg)
	);
	defparam uread_datapath.DEVICE_FAMILY                      	= DEVICE_FAMILY;
	defparam uread_datapath.MEM_ADDRESS_WIDTH                  	= MEM_ADDRESS_WIDTH; 
	defparam uread_datapath.MEM_DM_WIDTH                       	= MEM_DM_WIDTH; 
	defparam uread_datapath.MEM_CONTROL_WIDTH                  	= MEM_CONTROL_WIDTH; 
	defparam uread_datapath.MEM_DQ_WIDTH                       	= MEM_DQ_WIDTH; 
	defparam uread_datapath.MEM_READ_DQS_WIDTH                 	= MEM_READ_DQS_WIDTH; 
	defparam uread_datapath.MEM_WRITE_DQS_WIDTH                	= MEM_WRITE_DQS_WIDTH; 
	defparam uread_datapath.AFI_ADDRESS_WIDTH                  	= AFI_ADDRESS_WIDTH; 
	defparam uread_datapath.AFI_DATA_MASK_WIDTH                	= AFI_DATA_MASK_WIDTH; 
	defparam uread_datapath.AFI_CONTROL_WIDTH                  	= AFI_CONTROL_WIDTH; 
	defparam uread_datapath.AFI_DATA_WIDTH                     	= AFI_DATA_WIDTH; 
	defparam uread_datapath.AFI_DQS_WIDTH                     	= AFI_DQS_WIDTH;
	defparam uread_datapath.AFI_RATE_RATIO                     	= AFI_RATE_RATIO;
	defparam uread_datapath.MAX_LATENCY_COUNT_WIDTH            	= MAX_LATENCY_COUNT_WIDTH;
	defparam uread_datapath.MAX_READ_LATENCY					= MAX_READ_LATENCY;
	defparam uread_datapath.READ_FIFO_READ_MEM_DEPTH			= READ_FIFO_READ_MEM_DEPTH;
	defparam uread_datapath.READ_FIFO_READ_ADDR_WIDTH			= READ_FIFO_READ_ADDR_WIDTH;
	defparam uread_datapath.READ_FIFO_WRITE_MEM_DEPTH			= READ_FIFO_WRITE_MEM_DEPTH;
	defparam uread_datapath.READ_FIFO_WRITE_ADDR_WIDTH			= READ_FIFO_WRITE_ADDR_WIDTH;
	defparam uread_datapath.READ_VALID_FIFO_SIZE                = READ_VALID_FIFO_SIZE;
	defparam uread_datapath.READ_VALID_FIFO_READ_MEM_DEPTH		= READ_VALID_FIFO_READ_MEM_DEPTH;
	defparam uread_datapath.READ_VALID_FIFO_READ_ADDR_WIDTH		= READ_VALID_FIFO_READ_ADDR_WIDTH;
	defparam uread_datapath.READ_VALID_FIFO_WRITE_MEM_DEPTH		= READ_VALID_FIFO_WRITE_MEM_DEPTH;
	defparam uread_datapath.READ_VALID_FIFO_WRITE_ADDR_WIDTH	= READ_VALID_FIFO_WRITE_ADDR_WIDTH;    	
	defparam uread_datapath.READ_VALID_FIFO_PER_DQS_WIDTH		= READ_VALID_FIFO_PER_DQS_WIDTH;
	defparam uread_datapath.NUM_SUBGROUP_PER_READ_DQS			= NUM_SUBGROUP_PER_READ_DQS;  
	defparam uread_datapath.MEM_T_RL					        = MEM_T_RL;  
	defparam uread_datapath.CALIB_REG_WIDTH				        = CALIB_REG_WIDTH;
	defparam uread_datapath.QVLD_EXTRA_FLOP_STAGES				= QVLD_EXTRA_FLOP_STAGES;
	defparam uread_datapath.QVLD_WR_ADDRESS_OFFSET				= QVLD_WR_ADDRESS_OFFSET;
	defparam uread_datapath.REGISTER_C2P						= REGISTER_C2P;
	defparam uread_datapath.FAST_SIM_MODEL						= FAST_SIM_MODEL;
	defparam uread_datapath.EXTRA_VFIFO_SHIFT                   = EXTRA_VFIFO_SHIFT;


// ******************************************************************************************************************************** 
// The I/O block is responsible for instantiating all the built-in I/O logic in the FPGA
// ******************************************************************************************************************************** 
	nios_mem_if_ddr2_emif_0_p0_new_io_pads uio_pads (
                .reset_n_addr_cmd_clk   (reset_n_addr_cmd_clk),
		.reset_n_afi_clk		(reset_n_afi_clk[1]),
		.phy_reset_mem_stable   (phy_reset_mem_stable),

        .oct_ctl_rs_value       (oct_ctl_rs_value),
        .oct_ctl_rt_value       (oct_ctl_rt_value),

		// Address and Command
		.phy_ddio_addr_cmd_clk	(pll_addr_cmd_clk),

		.phy_ddio_address 		(phy_ddio_address),
		.phy_ddio_bank   	 	(phy_ddio_bank),
		.phy_ddio_cs_n    		(phy_ddio_cs_n),
		.phy_ddio_cke    		(phy_ddio_cke),
		.phy_ddio_odt    		(phy_ddio_odt),
		.phy_ddio_we_n    		(phy_ddio_we_n),
		.phy_ddio_ras_n   		(phy_ddio_ras_n),
		.phy_ddio_cas_n   		(phy_ddio_cas_n),

		.phy_mem_address    	(mem_a),
		.phy_mem_bank	    	(mem_ba),
		.phy_mem_cs_n	    	(mem_cs_n),
		.phy_mem_cke	    	(mem_cke),
		.phy_mem_odt	    	(mem_odt),
		.phy_mem_we_n	    	(mem_we_n),
		.phy_mem_ras_n	    	(mem_ras_n),
		.phy_mem_cas_n	    	(mem_cas_n),

		// Write
		.pll_afi_clk	    	(pll_afi_clk),
		.pll_mem_clk	    	(pll_mem_clk),
		.pll_write_clk	    	(pll_write_clk),
		.pll_mem_phy_clk		(pll_mem_phy_clk),
		.pll_afi_phy_clk		(pll_afi_phy_clk),
		.pll_dqs_ena_clk		(pll_dqs_ena_clk),
		.phy_ddio_dq	    	(phy_ddio_dq),
		.phy_ddio_dqs_en		(phy_ddio_dqs_en),
		.phy_ddio_oct_ena		(phy_ddio_oct_ena),
		.dqs_enable_ctrl		(dqs_enable_ctrl),
        .phy_ddio_wrdata_en 	(phy_ddio_wrdata_en),
        .phy_ddio_wrdata_mask   (phy_ddio_wrdata_mask),


        .phy_mem_dq             (mem_dq),
        .phy_mem_dm             (mem_dm),
        .phy_mem_ck             (mem_ck),
        .phy_mem_ck_n           (mem_ck_n),
		.mem_dqs				(mem_dqs),
		.mem_dqs_n				(mem_dqs_n),

		// Read
		.dll_phy_delayctrl		(dll_phy_delayctrl),
		.ddio_phy_dq			(ddio_phy_dq),
		.read_capture_clk       (read_capture_clk)
        ,
		
		.scc_clk				(pll_config_clk),       
		.scc_data               (scc_data),
		.scc_dqs_ena            (scc_dqs_ena),
		.scc_dqs_io_ena         (scc_dqs_io_ena),
		.scc_dq_ena             (scc_dq_ena),
		.scc_dm_ena             (scc_dm_ena),
		.scc_upd                (scc_upd),
		.scc_sr_dqsenable_delayctrl (scc_sr_dqsenable_delayctrl),
		.scc_sr_dqsdisablen_delayctrl (scc_sr_dqsdisablen_delayctrl),
		.scc_sr_multirank_delayctrl (scc_sr_multirank_delayctrl),
		.afi_rdata_en					(afi_rdata_en),
		.afi_rdata_en_full				(afi_rdata_en_full),
		.seq_read_latency_counter		(phy_read_latency_counter),
		.seq_read_fifo_reset			(phy_read_fifo_reset),
		.seq_read_increment_vfifo_fr	(phy_read_increment_vfifo_fr),
		.seq_read_increment_vfifo_hr	(phy_read_increment_vfifo_hr),
		.enable_mem_clk                (~afi_mem_clk_disable),
		.capture_strobe_tracking	(capture_strobe_tracking)
    );

        defparam uio_pads.DEVICE_FAMILY                      = DEVICE_FAMILY;
        defparam uio_pads.OCT_SERIES_TERM_CONTROL_WIDTH      = OCT_SERIES_TERM_CONTROL_WIDTH;
        defparam uio_pads.OCT_PARALLEL_TERM_CONTROL_WIDTH    = OCT_PARALLEL_TERM_CONTROL_WIDTH;
        defparam uio_pads.MEM_ADDRESS_WIDTH                  = MEM_ADDRESS_WIDTH;
        defparam uio_pads.MEM_BANK_WIDTH                     = MEM_BANK_WIDTH;
        defparam uio_pads.MEM_CHIP_SELECT_WIDTH              = MEM_CHIP_SELECT_WIDTH;
        defparam uio_pads.MEM_CLK_EN_WIDTH                   = MEM_CLK_EN_WIDTH;
        defparam uio_pads.MEM_CK_WIDTH                       = MEM_CK_WIDTH;
        defparam uio_pads.MEM_ODT_WIDTH                      = MEM_ODT_WIDTH;
        defparam uio_pads.MEM_DQS_WIDTH                      = MEM_DQS_WIDTH;
        defparam uio_pads.MEM_DM_WIDTH                       = MEM_DM_WIDTH;
        defparam uio_pads.MEM_CONTROL_WIDTH                  = MEM_CONTROL_WIDTH;
        defparam uio_pads.MEM_DQ_WIDTH                       = MEM_DQ_WIDTH;
        defparam uio_pads.MEM_READ_DQS_WIDTH                 = MEM_READ_DQS_WIDTH;
        defparam uio_pads.MEM_WRITE_DQS_WIDTH                = MEM_WRITE_DQS_WIDTH;
        defparam uio_pads.AFI_ADDRESS_WIDTH                  = AFI_ADDRESS_WIDTH;
        defparam uio_pads.AFI_BANK_WIDTH                     = AFI_BANK_WIDTH;
        defparam uio_pads.AFI_CHIP_SELECT_WIDTH              = AFI_CHIP_SELECT_WIDTH;
        defparam uio_pads.AFI_CLK_EN_WIDTH                   = AFI_CLK_EN_WIDTH;
        defparam uio_pads.AFI_ODT_WIDTH                      = AFI_ODT_WIDTH;
        defparam uio_pads.AFI_DATA_MASK_WIDTH                = AFI_DATA_MASK_WIDTH;
        defparam uio_pads.AFI_CONTROL_WIDTH                  = AFI_CONTROL_WIDTH;
        defparam uio_pads.AFI_DATA_WIDTH                     = AFI_DATA_WIDTH;
        defparam uio_pads.AFI_DQS_WIDTH                      = AFI_DQS_WIDTH;
        defparam uio_pads.AFI_RATE_RATIO                     = AFI_RATE_RATIO;
        defparam uio_pads.DLL_DELAY_CTRL_WIDTH               = DLL_DELAY_CTRL_WIDTH;
        defparam uio_pads.SCC_DATA_WIDTH                     = SCC_DATA_WIDTH;
        defparam uio_pads.REGISTER_C2P                       = REGISTER_C2P;
        defparam uio_pads.LDC_MEM_CK_CPS_PHASE               = LDC_MEM_CK_CPS_PHASE;
        defparam uio_pads.DQS_ENABLE_CTRL_WIDTH              = READ_VALID_FIFO_WIDTH;
        defparam uio_pads.ALTDQDQS_INPUT_FREQ                = ALTDQDQS_INPUT_FREQ;
        defparam uio_pads.ALTDQDQS_DELAY_CHAIN_BUFFER_MODE   = ALTDQDQS_DELAY_CHAIN_BUFFER_MODE;
        defparam uio_pads.ALTDQDQS_DQS_PHASE_SETTING         = ALTDQDQS_DQS_PHASE_SETTING;
        defparam uio_pads.ALTDQDQS_DQS_PHASE_SHIFT           = ALTDQDQS_DQS_PHASE_SHIFT;
        defparam uio_pads.ALTDQDQS_DELAYED_CLOCK_PHASE_SETTING = ALTDQDQS_DELAYED_CLOCK_PHASE_SETTING;
        defparam uio_pads.EXTRA_VFIFO_SHIFT                  = EXTRA_VFIFO_SHIFT;
        defparam uio_pads.FAST_SIM_MODEL                     = FAST_SIM_MODEL;
        defparam uio_pads.IS_HHP_HPS                         = "false"; 




	 






// Calculate the ceiling of log_2 of the input value
function integer ceil_log2;
	input integer value;
	begin
		value = value - 1;
		for (ceil_log2 = 0; value > 0; ceil_log2 = ceil_log2 + 1)
			value = value >> 1;
	end
endfunction

endmodule
