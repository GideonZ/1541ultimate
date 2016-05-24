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

// altera message_off 10036
module nios_mem_if_ddr2_emif_0_p0_new_io_pads(
	reset_n_addr_cmd_clk,
	reset_n_afi_clk,
	phy_reset_mem_stable,
	oct_ctl_rs_value,
	oct_ctl_rt_value,
	phy_ddio_addr_cmd_clk,
	phy_ddio_address,
	phy_ddio_bank,
	phy_ddio_cs_n,
	phy_ddio_cke,
	phy_ddio_odt,
	phy_ddio_we_n,
	phy_ddio_ras_n,
	phy_ddio_cas_n,
	phy_mem_address,
	phy_mem_bank,
	phy_mem_cs_n,
	phy_mem_cke,
	phy_mem_odt,
	phy_mem_we_n,
	phy_mem_ras_n,
	phy_mem_cas_n,
	pll_afi_clk,
	pll_mem_clk,
	pll_write_clk,
	pll_mem_phy_clk,
	pll_afi_phy_clk,
	pll_dqs_ena_clk,
	phy_ddio_dq,
	phy_ddio_dqs_en,
	phy_ddio_oct_ena,
	dqs_enable_ctrl,
	phy_ddio_wrdata_en,
	phy_ddio_wrdata_mask,
	phy_mem_dq,
	phy_mem_dm,
	phy_mem_ck,
	phy_mem_ck_n,
	mem_dqs,
	mem_dqs_n,
	dll_phy_delayctrl,
	ddio_phy_dq,
	read_capture_clk, 
	scc_clk,
	scc_data,
	scc_dqs_ena,
	scc_dqs_io_ena,
	scc_dq_ena,
	scc_dm_ena,
	scc_sr_dqsenable_delayctrl,
	scc_sr_dqsdisablen_delayctrl,
	scc_sr_multirank_delayctrl,
	scc_upd,
	afi_rdata_en,
	afi_rdata_en_full,
	afi_rdata_valid,
	seq_read_latency_counter,
	seq_read_increment_vfifo_fr,
	seq_read_increment_vfifo_hr,
	seq_read_fifo_reset,
    enable_mem_clk,
	capture_strobe_tracking
);


parameter DEVICE_FAMILY = "";
parameter REGISTER_C2P = "";
parameter LDC_MEM_CK_CPS_PHASE = "";

parameter OCT_SERIES_TERM_CONTROL_WIDTH = "";  
parameter OCT_PARALLEL_TERM_CONTROL_WIDTH = ""; 
parameter MEM_ADDRESS_WIDTH     = ""; 
parameter MEM_BANK_WIDTH        = ""; 
parameter MEM_CHIP_SELECT_WIDTH = ""; 
parameter MEM_CLK_EN_WIDTH 		= ""; 
parameter MEM_CK_WIDTH 			= ""; 
parameter MEM_ODT_WIDTH 		= ""; 
parameter MEM_DQS_WIDTH			= "";
parameter MEM_DM_WIDTH          = ""; 
parameter MEM_CONTROL_WIDTH     = ""; 
parameter MEM_DQ_WIDTH          = ""; 
parameter MEM_READ_DQS_WIDTH    = ""; 
parameter MEM_WRITE_DQS_WIDTH   = ""; 

parameter AFI_ADDRESS_WIDTH         = ""; 
parameter AFI_BANK_WIDTH            = ""; 
parameter AFI_CHIP_SELECT_WIDTH     = ""; 
parameter AFI_CLK_EN_WIDTH 			= ""; 
parameter AFI_ODT_WIDTH 			= ""; 
parameter AFI_DATA_MASK_WIDTH       = ""; 
parameter AFI_CONTROL_WIDTH         = ""; 
parameter AFI_DATA_WIDTH            = ""; 
parameter AFI_DQS_WIDTH             = ""; 
parameter AFI_RATE_RATIO            = ""; 

parameter DLL_DELAY_CTRL_WIDTH  = "";

parameter SCC_DATA_WIDTH        = "";

parameter DQS_ENABLE_CTRL_WIDTH = "";
parameter ALTDQDQS_INPUT_FREQ = "";
parameter ALTDQDQS_DELAY_CHAIN_BUFFER_MODE = "";
parameter ALTDQDQS_DQS_PHASE_SETTING = "";
parameter ALTDQDQS_DQS_PHASE_SHIFT = "";
parameter ALTDQDQS_DELAYED_CLOCK_PHASE_SETTING = "";

parameter FAST_SIM_MODEL            = "";

parameter EXTRA_VFIFO_SHIFT = 0;

parameter IS_HHP_HPS = "";

localparam DOUBLE_MEM_DQ_WIDTH = MEM_DQ_WIDTH * 2;
localparam HALF_AFI_DATA_WIDTH = AFI_DATA_WIDTH / 2;
localparam HALF_AFI_DQS_WIDTH = AFI_DQS_WIDTH / 2;



input	reset_n_afi_clk;
input	reset_n_addr_cmd_clk;
input   phy_reset_mem_stable;

input   [OCT_SERIES_TERM_CONTROL_WIDTH-1:0] oct_ctl_rs_value;
input   [OCT_PARALLEL_TERM_CONTROL_WIDTH-1:0] oct_ctl_rt_value;

input	phy_ddio_addr_cmd_clk;
input	[AFI_ADDRESS_WIDTH-1:0]	phy_ddio_address;
input	[AFI_BANK_WIDTH-1:0]    phy_ddio_bank;
input	[AFI_CHIP_SELECT_WIDTH-1:0] phy_ddio_cs_n;
input	[AFI_CLK_EN_WIDTH-1:0] phy_ddio_cke;
input	[AFI_ODT_WIDTH-1:0] phy_ddio_odt;
input	[AFI_CONTROL_WIDTH-1:0] phy_ddio_ras_n;
input	[AFI_CONTROL_WIDTH-1:0] phy_ddio_cas_n;
input	[AFI_CONTROL_WIDTH-1:0] phy_ddio_we_n;

output  [MEM_ADDRESS_WIDTH-1:0]	phy_mem_address;
output	[MEM_BANK_WIDTH-1:0]	phy_mem_bank;
output	[MEM_CHIP_SELECT_WIDTH-1:0]	phy_mem_cs_n;
output  [MEM_CLK_EN_WIDTH-1:0]	phy_mem_cke;
output  [MEM_ODT_WIDTH-1:0]	phy_mem_odt;
output	[MEM_CONTROL_WIDTH-1:0]	phy_mem_we_n;
output	[MEM_CONTROL_WIDTH-1:0] phy_mem_ras_n;
output	[MEM_CONTROL_WIDTH-1:0] phy_mem_cas_n;

input	pll_afi_clk;
input	pll_mem_clk;
input	pll_write_clk;
input pll_mem_phy_clk;
input pll_afi_phy_clk;
input	pll_dqs_ena_clk;
input	[AFI_DATA_WIDTH-1:0]  phy_ddio_dq;
input	[AFI_DQS_WIDTH-1:0] phy_ddio_dqs_en;
input	[AFI_DQS_WIDTH-1:0] phy_ddio_oct_ena;
input	[DQS_ENABLE_CTRL_WIDTH-1:0] dqs_enable_ctrl;
input	[AFI_DQS_WIDTH-1:0] phy_ddio_wrdata_en;
input	[AFI_DATA_MASK_WIDTH-1:0]	phy_ddio_wrdata_mask;	


inout	[MEM_DQ_WIDTH-1:0]	phy_mem_dq;
output	[MEM_DM_WIDTH-1:0]	phy_mem_dm;
output	[MEM_CK_WIDTH-1:0]	phy_mem_ck;
output	[MEM_CK_WIDTH-1:0]	phy_mem_ck_n;
inout	[MEM_DQS_WIDTH-1:0]	mem_dqs;
inout	[MEM_DQS_WIDTH-1:0]	mem_dqs_n;

input   [DLL_DELAY_CTRL_WIDTH-1:0]  dll_phy_delayctrl;
localparam DDIO_PHY_DQ_WIDTH = AFI_DATA_WIDTH;
output	[DDIO_PHY_DQ_WIDTH-1:0] ddio_phy_dq;	
output	[MEM_READ_DQS_WIDTH-1:0] read_capture_clk;	

input	scc_clk;
input	[SCC_DATA_WIDTH - 1:0] scc_data;
input	[MEM_READ_DQS_WIDTH - 1:0] scc_dqs_ena;
input	[MEM_READ_DQS_WIDTH - 1:0] scc_dqs_io_ena;
input	[MEM_DQ_WIDTH - 1:0] scc_dq_ena;
input	[MEM_DM_WIDTH - 1:0] scc_dm_ena;
input   [7:0] scc_sr_dqsenable_delayctrl;
input   [7:0] scc_sr_dqsdisablen_delayctrl;
input   [7:0] scc_sr_multirank_delayctrl;

input  [AFI_RATE_RATIO-1:0] afi_rdata_en;
input  [AFI_RATE_RATIO-1:0] afi_rdata_en_full;
output [AFI_RATE_RATIO-1:0] afi_rdata_valid;
input [4:0] seq_read_latency_counter;
input seq_read_fifo_reset;
input [MEM_READ_DQS_WIDTH-1:0] seq_read_increment_vfifo_fr;
input [MEM_READ_DQS_WIDTH-1:0] seq_read_increment_vfifo_hr;
wire [MEM_READ_DQS_WIDTH-1:0] rdata_valid;
input [0:0] scc_upd;
input   [MEM_CK_WIDTH-1:0] enable_mem_clk;
output	[MEM_READ_DQS_WIDTH - 1:0] capture_strobe_tracking;

assign capture_strobe_tracking = 1'd0;

wire	[MEM_DQ_WIDTH-1:0] mem_phy_dq;
wire	[DLL_DELAY_CTRL_WIDTH-1:0] read_bidir_dll_phy_delayctrl;
wire	[MEM_READ_DQS_WIDTH-1:0] bidir_read_dqs_bus_out;
wire	[MEM_DQ_WIDTH-1:0] bidir_read_dq_input_data_out_high;
wire	[MEM_DQ_WIDTH-1:0] bidir_read_dq_input_data_out_low;

wire	hr_clk = pll_afi_clk;
wire	core_clk = pll_afi_clk;
wire	reset_n_core_clk = reset_n_afi_clk;

reg [AFI_DATA_WIDTH-1:0] phy_ddio_dq_int;
reg [AFI_DQS_WIDTH-1:0] phy_ddio_wrdata_en_int;
reg [AFI_DATA_MASK_WIDTH-1:0] phy_ddio_wrdata_mask_int;
reg [AFI_DQS_WIDTH-1:0] phy_ddio_dqs_en_int;
reg [AFI_DQS_WIDTH-1:0] phy_ddio_oct_ena_int;
	
generate
if (REGISTER_C2P == "false") begin
	always @(*) begin	
		phy_ddio_dq_int = phy_ddio_dq;
		phy_ddio_wrdata_en_int = phy_ddio_wrdata_en;
		phy_ddio_wrdata_mask_int = phy_ddio_wrdata_mask;	
		phy_ddio_dqs_en_int = phy_ddio_dqs_en;
		phy_ddio_oct_ena_int = phy_ddio_oct_ena;
	end
	
end else begin

	always @(posedge pll_afi_clk) begin
		phy_ddio_dq_int <= phy_ddio_dq;
		phy_ddio_wrdata_en_int <= phy_ddio_wrdata_en;
		phy_ddio_wrdata_mask_int <= phy_ddio_wrdata_mask;	
		phy_ddio_dqs_en_int <= phy_ddio_dqs_en;
		phy_ddio_oct_ena_int <= phy_ddio_oct_ena;
	end
end
endgenerate	


wire afi_rdata_en_full_shifted_int;

generate
if (EXTRA_VFIFO_SHIFT > 1) begin
	reg [EXTRA_VFIFO_SHIFT-1:0] extra_vfifo_shift;
	always @(posedge pll_afi_clk or negedge reset_n_afi_clk)
	begin
		if (~reset_n_afi_clk) begin
			extra_vfifo_shift <= {EXTRA_VFIFO_SHIFT{1'b0}};
		end
		else begin
			extra_vfifo_shift <= {extra_vfifo_shift[EXTRA_VFIFO_SHIFT-2:0], afi_rdata_en_full[0]};
		end
	end
	assign afi_rdata_en_full_shifted_int = extra_vfifo_shift[EXTRA_VFIFO_SHIFT-1];
end else if (EXTRA_VFIFO_SHIFT == 1) begin
	reg [EXTRA_VFIFO_SHIFT-1:0] extra_vfifo_shift;
	always @(posedge pll_afi_clk or negedge reset_n_afi_clk)
	begin
		if (~reset_n_afi_clk) begin
			extra_vfifo_shift <= {EXTRA_VFIFO_SHIFT{1'b0}};
		end
		else begin
			extra_vfifo_shift <= {afi_rdata_en_full[0]};
		end
	end
	assign afi_rdata_en_full_shifted_int = extra_vfifo_shift[EXTRA_VFIFO_SHIFT-1];
end else begin
	assign afi_rdata_en_full_shifted_int = afi_rdata_en_full[0];
end
endgenerate

wire afi_rdata_en_full_shifted = afi_rdata_en_full_shifted_int;

	nios_mem_if_ddr2_emif_0_p0_addr_cmd_pads uaddr_cmd_pads(
		.reset_n				(reset_n_addr_cmd_clk),
		.reset_n_afi_clk		(reset_n_afi_clk),
		.pll_afi_clk            (pll_afi_clk),
		.pll_mem_clk            (pll_mem_clk),
		.pll_mem_phy_clk        (pll_mem_phy_clk),
		.pll_afi_phy_clk        (pll_afi_phy_clk),
		.pll_write_clk          (pll_write_clk),
		.phy_ddio_addr_cmd_clk  (phy_ddio_addr_cmd_clk),
		.dll_delayctrl_in       (dll_phy_delayctrl),
		.enable_mem_clk         (enable_mem_clk),
		.phy_ddio_address 		(phy_ddio_address),
		.phy_ddio_bank		    (phy_ddio_bank),
		.phy_ddio_cs_n		    (phy_ddio_cs_n),
		.phy_ddio_cke			(phy_ddio_cke),
		.phy_ddio_odt			(phy_ddio_odt),
		.phy_ddio_we_n		    (phy_ddio_we_n),	
		.phy_ddio_ras_n		    (phy_ddio_ras_n),
		.phy_ddio_cas_n		    (phy_ddio_cas_n),

		.phy_mem_address		(phy_mem_address),
		.phy_mem_bank			(phy_mem_bank),
		.phy_mem_cs_n			(phy_mem_cs_n),
		.phy_mem_cke			(phy_mem_cke),
		.phy_mem_odt			(phy_mem_odt),
		.phy_mem_we_n			(phy_mem_we_n),
		.phy_mem_ras_n			(phy_mem_ras_n),
		.phy_mem_cas_n			(phy_mem_cas_n),
		.phy_mem_ck				(phy_mem_ck),
		.phy_mem_ck_n			(phy_mem_ck_n)
	);
	defparam uaddr_cmd_pads.DEVICE_FAMILY			= DEVICE_FAMILY;
	defparam uaddr_cmd_pads.MEM_ADDRESS_WIDTH		= MEM_ADDRESS_WIDTH;
	defparam uaddr_cmd_pads.MEM_BANK_WIDTH			= MEM_BANK_WIDTH;
	defparam uaddr_cmd_pads.MEM_CHIP_SELECT_WIDTH	= MEM_CHIP_SELECT_WIDTH;
	defparam uaddr_cmd_pads.MEM_CLK_EN_WIDTH		= MEM_CLK_EN_WIDTH;
	defparam uaddr_cmd_pads.MEM_CK_WIDTH			= MEM_CK_WIDTH;
	defparam uaddr_cmd_pads.MEM_ODT_WIDTH			= MEM_ODT_WIDTH;
	defparam uaddr_cmd_pads.MEM_CONTROL_WIDTH		= MEM_CONTROL_WIDTH;
	defparam uaddr_cmd_pads.AFI_ADDRESS_WIDTH       = AFI_ADDRESS_WIDTH; 
	defparam uaddr_cmd_pads.AFI_BANK_WIDTH          = AFI_BANK_WIDTH; 
	defparam uaddr_cmd_pads.AFI_CHIP_SELECT_WIDTH   = AFI_CHIP_SELECT_WIDTH; 
	defparam uaddr_cmd_pads.AFI_CLK_EN_WIDTH        = AFI_CLK_EN_WIDTH; 
	defparam uaddr_cmd_pads.AFI_ODT_WIDTH           = AFI_ODT_WIDTH; 
	defparam uaddr_cmd_pads.AFI_CONTROL_WIDTH       = AFI_CONTROL_WIDTH; 
	defparam uaddr_cmd_pads.DLL_WIDTH               = DLL_DELAY_CTRL_WIDTH; 
	defparam uaddr_cmd_pads.REGISTER_C2P            = REGISTER_C2P;
	defparam uaddr_cmd_pads.IS_HHP_HPS              = IS_HHP_HPS;
		
	localparam NUM_OF_DQDQS = MEM_WRITE_DQS_WIDTH;
	localparam DQDQS_DATA_WIDTH = MEM_DQ_WIDTH / NUM_OF_DQDQS;
	localparam DQDQS_DDIO_PHY_DQ_WIDTH = DDIO_PHY_DQ_WIDTH / NUM_OF_DQDQS;
	
	localparam DQDQS_DM_WIDTH = MEM_DM_WIDTH / MEM_WRITE_DQS_WIDTH;
		
	localparam NUM_OF_DQDQS_WITH_DM = MEM_WRITE_DQS_WIDTH;		

	generate
	genvar i;
	for (i=0; i<NUM_OF_DQDQS; i=i+1)
	begin: dq_ddio
		wire dqs_busout;

		// The phy_ddio_dq_int bus is the write data for all DQS groups in one
		// AFI cycle. The bus is ordered by time slot and subordered by DQS group:
		//
		// FR: D1_T1, D0_T1, D1_T0, D0_T0
		// HR: D1_T3, D0_T3, D1_T2, D0_T2, D1_T1, D0_T1, D1_T0, D0_T0
		//
		// Extract the write data targeting the current DQS group
		wire [DQDQS_DATA_WIDTH-1:0] phy_ddio_dq_t0 = phy_ddio_dq_int [DQDQS_DATA_WIDTH*(i+1+0*NUM_OF_DQDQS)-1 : DQDQS_DATA_WIDTH*(i+0*NUM_OF_DQDQS)];
		wire [DQDQS_DATA_WIDTH-1:0] phy_ddio_dq_t1 = phy_ddio_dq_int [DQDQS_DATA_WIDTH*(i+1+1*NUM_OF_DQDQS)-1 : DQDQS_DATA_WIDTH*(i+1*NUM_OF_DQDQS)];
		wire [DQDQS_DATA_WIDTH-1:0] phy_ddio_dq_t2 = phy_ddio_dq_int [DQDQS_DATA_WIDTH*(i+1+2*NUM_OF_DQDQS)-1 : DQDQS_DATA_WIDTH*(i+2*NUM_OF_DQDQS)];
		wire [DQDQS_DATA_WIDTH-1:0] phy_ddio_dq_t3 = phy_ddio_dq_int [DQDQS_DATA_WIDTH*(i+1+3*NUM_OF_DQDQS)-1 : DQDQS_DATA_WIDTH*(i+3*NUM_OF_DQDQS)];

		// Extract the OE signal targeting the current DQS group
		wire [DQDQS_DATA_WIDTH-1:0] phy_ddio_wrdata_en_t0 = {DQDQS_DATA_WIDTH{phy_ddio_wrdata_en_int[i]}};
		wire [DQDQS_DATA_WIDTH-1:0] phy_ddio_wrdata_en_t1 = {DQDQS_DATA_WIDTH{phy_ddio_wrdata_en_int[i+MEM_WRITE_DQS_WIDTH]}};

		// Extract the dynamic OCT control signal targeting the current DQS group
		wire phy_ddio_oct_ena_t0 = phy_ddio_oct_ena_int[i];
		wire phy_ddio_oct_ena_t1 = phy_ddio_oct_ena_int[i+MEM_WRITE_DQS_WIDTH];

		// Extract the write data mask signal targeting the current DQS group
		wire [DQDQS_DM_WIDTH-1:0] phy_ddio_wrdata_mask_t0;
		wire [DQDQS_DM_WIDTH-1:0] phy_ddio_wrdata_mask_t1;
		wire [DQDQS_DM_WIDTH-1:0] phy_ddio_wrdata_mask_t2;
		wire [DQDQS_DM_WIDTH-1:0] phy_ddio_wrdata_mask_t3;
		assign phy_ddio_wrdata_mask_t0 = phy_ddio_wrdata_mask_int [DQDQS_DM_WIDTH*(i+1+0*NUM_OF_DQDQS_WITH_DM)-1 : DQDQS_DM_WIDTH*(i+0*NUM_OF_DQDQS_WITH_DM)];
		assign phy_ddio_wrdata_mask_t1 = phy_ddio_wrdata_mask_int [DQDQS_DM_WIDTH*(i+1+1*NUM_OF_DQDQS_WITH_DM)-1 : DQDQS_DM_WIDTH*(i+1*NUM_OF_DQDQS_WITH_DM)];
		assign phy_ddio_wrdata_mask_t2 = phy_ddio_wrdata_mask_int [DQDQS_DM_WIDTH*(i+1+2*NUM_OF_DQDQS_WITH_DM)-1 : DQDQS_DM_WIDTH*(i+2*NUM_OF_DQDQS_WITH_DM)];
		assign phy_ddio_wrdata_mask_t3 = phy_ddio_wrdata_mask_int [DQDQS_DM_WIDTH*(i+1+3*NUM_OF_DQDQS_WITH_DM)-1 : DQDQS_DM_WIDTH*(i+3*NUM_OF_DQDQS_WITH_DM)];

	wire [AFI_RATE_RATIO-1:0] lfifo_rdata_en = {AFI_RATE_RATIO{afi_rdata_en[0]}};
	wire [AFI_RATE_RATIO-1:0] lfifo_rdata_en_full = {AFI_RATE_RATIO{afi_rdata_en_full_shifted}};
	wire [AFI_RATE_RATIO-1:0] vfifo_qvld = {AFI_RATE_RATIO{afi_rdata_en_full_shifted}};

	wire [1:0] vfifo_inc_wr_ptr = {1'b0,seq_read_increment_vfifo_fr[i]};


			nios_mem_if_ddr2_emif_0_p0_altdqdqs ubidir_dq_dqs (
				.write_strobe_clock_in (pll_mem_phy_clk),
				.reset_n_core_clock_in (reset_n_core_clk),
				.core_clock_in (core_clk),
				.fr_clock_in (pll_write_clk),

				.hr_clock_in (pll_afi_phy_clk),
				.parallelterminationcontrol_in(oct_ctl_rt_value),
				.seriesterminationcontrol_in(oct_ctl_rs_value),
				.strobe_ena_hr_clock_in (hr_clk),
				.read_write_data_io (phy_mem_dq[(DQDQS_DATA_WIDTH*(i+1)-1) : DQDQS_DATA_WIDTH*i]),
				.read_data_out (ddio_phy_dq [(DQDQS_DDIO_PHY_DQ_WIDTH*(i+1)-1) : DQDQS_DDIO_PHY_DQ_WIDTH*i]),
				.capture_strobe_out(dqs_busout),
			
				.extra_write_data_in ({phy_ddio_wrdata_mask_t3, phy_ddio_wrdata_mask_t2, phy_ddio_wrdata_mask_t1, phy_ddio_wrdata_mask_t0}),
				
				.write_data_in ({phy_ddio_dq_t3, phy_ddio_dq_t2, phy_ddio_dq_t1, phy_ddio_dq_t0}),

				.write_oe_in ({phy_ddio_wrdata_en_t1, phy_ddio_wrdata_en_t0}),

				.strobe_io (mem_dqs[i]),
				.strobe_n_io (mem_dqs_n[i]),
				.output_strobe_ena ({phy_ddio_dqs_en_int[i+NUM_OF_DQDQS], phy_ddio_dqs_en_int[i]}),
				.oct_ena_in ({phy_ddio_oct_ena_t1, phy_ddio_oct_ena_t0}),
				.extra_write_data_out (phy_mem_dm[i]),
				.config_data_in (scc_data),
				.config_dqs_ena (scc_dqs_ena[i]),
				.config_io_ena (scc_dq_ena[(DQDQS_DATA_WIDTH*(i+1)-1) : DQDQS_DATA_WIDTH*i]),
				.config_dqs_io_ena (scc_dqs_io_ena[i]),
				.config_update (scc_upd[0]),
				.config_clock_in (scc_clk),
				.config_extra_io_ena (scc_dm_ena[i]),
				.lfifo_rdata_en (lfifo_rdata_en),
				.lfifo_rdata_en_full (lfifo_rdata_en_full),
				.lfifo_rd_latency (seq_read_latency_counter),
				.lfifo_reset_n (~reset_n_afi_clk),
				.lfifo_rdata_valid (rdata_valid[i]),
				.vfifo_qvld (vfifo_qvld),
				.vfifo_inc_wr_ptr (vfifo_inc_wr_ptr),
				.vfifo_reset_n (~phy_reset_mem_stable),
				.rfifo_reset_n (seq_read_fifo_reset),

				.dll_delayctrl_in (dll_phy_delayctrl)	
				);
			defparam ubidir_dq_dqs.ALTERA_ALTDQ_DQS2_FAST_SIM_MODEL = FAST_SIM_MODEL;
						
			
		assign read_capture_clk[i] = ~dqs_busout;
	end
	endgenerate


		reg [AFI_RATE_RATIO-1:0] afi_rdata_valid;
		always @(posedge pll_mem_clk or negedge reset_n_afi_clk)
		begin
			if (~reset_n_afi_clk) begin	
				afi_rdata_valid <= {AFI_RATE_RATIO{1'b0}};
			end else begin
				afi_rdata_valid <= {AFI_RATE_RATIO{&rdata_valid}};
			end
		end
endmodule
