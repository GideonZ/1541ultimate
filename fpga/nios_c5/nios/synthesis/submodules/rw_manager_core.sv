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
(* altera_attribute = "-name FITTER_ADJUST_HC_SHORT_PATH_GUARDBAND 270" *)
module rw_manager_core (
	avl_clk,
	avl_reset_n,
	avl_address,
	avl_readdata,
	avl_writedata,
 
	afi_clk,
	afi_reset_n,
	
	afi_wdata,
	afi_dm,
	afi_rdata,
	afi_rdata_valid,
	afi_rrank,
	afi_wrank,

	afi_odt,

	ac_masked_bus,
	ac_bus,
	
	cmd_read,
	cmd_write,
	cmd_done,
	csr_clk,
	csr_ena,
	csr_dout_phy,
	csr_dout,


	cs_mask_export,
	jump_ptr_0_export, 	
	jump_ptr_1_export, 
	jump_cntr_0_export, 	
	jump_cntr_1_export
);

	parameter AVL_DATA_WIDTH			= "";
	parameter AVL_ADDRESS_WIDTH			= "";

	parameter MEM_DQ_WIDTH				= "";
	parameter MEM_DM_WIDTH				= "";
	parameter MEM_ODT_WIDTH 			= "";
	parameter MEM_NUMBER_OF_RANKS		= "";
	
	parameter AC_ODT_BIT				= "";
	parameter AC_BUS_WIDTH				= "";
	parameter AC_MASKED_BUS_WIDTH			= "";
	parameter MASK_WIDTH				= "";
	parameter AFI_RATIO				= "";

	parameter MEM_READ_DQS_WIDTH 			= "";
	parameter MEM_WRITE_DQS_WIDTH			= "";

	parameter DEBUG_READ_DI_WIDTH			= "";
	parameter DEBUG_WRITE_TO_READ_RATIO_2_EXPONENT	= "";
	parameter DEBUG_WRITE_TO_READ_RATIO	= "";
	parameter MAX_DI_BUFFER_WORDS_LOG_2 = "";

	parameter RATE = "";
	parameter HCX_COMPAT_MODE = 0;
	parameter DEVICE_FAMILY = "";

	parameter AC_ROM_INIT_FILE_NAME = "AC_ROM.hex";
	parameter INST_ROM_INIT_FILE_NAME = "inst_ROM.hex";
	
	parameter USE_ALL_AFI_PHASES_FOR_COMMAND_ISSUE = 0;

	parameter AP_MODE_EN = 2'b00;

	localparam AC_ROM_DATA_WIDTH = AC_BUS_WIDTH + ((RATE == "Quarter") ? 2 : AFI_RATIO);
	localparam AC_ROM_FIXED_DATA_WIDTH = 32;
	
	localparam AC_ROM_ADDRESS_WIDTH = 6;
	localparam INST_ROM_DATA_WIDTH = 20;
	
        localparam INST_ROM_ADDRESS_WIDTH = 7;
	localparam JUMP_COUNTER_WIDTH = 8;

	localparam GROUP_COUNTER_WIDTH = 8;
	localparam NUMBER_OF_READ_DQ_PER_DQS = MEM_DQ_WIDTH / MEM_READ_DQS_WIDTH;
	localparam NUMBER_OF_WRITE_DQ_PER_DQS = MEM_DQ_WIDTH / MEM_WRITE_DQS_WIDTH;

	localparam NUMBER_OF_WORDS = 2 * AFI_RATIO;
	localparam GROUP_BUS_SIZE = NUMBER_OF_READ_DQ_PER_DQS * NUMBER_OF_WORDS;

	localparam CS_MASK_WIDTH = 8;
	
	localparam MAX_DI_BUFFER_WORDS = 2**MAX_DI_BUFFER_WORDS_LOG_2;
	localparam DI_ADDR_WIDTH = MAX_DI_BUFFER_WORDS_LOG_2;
	

	input avl_clk;
	input avl_reset_n;
	input [AVL_DATA_WIDTH - 1:0] avl_writedata;
	output [AVL_DATA_WIDTH - 1:0] avl_readdata;

	// AFI interface portion

	input afi_clk;
	input afi_reset_n;
	
	output [AC_BUS_WIDTH - 1:0] ac_bus;
	output [AC_MASKED_BUS_WIDTH - 1:0] ac_masked_bus;

	output [MEM_DQ_WIDTH * 2 * AFI_RATIO - 1:0] afi_wdata;
	output [MEM_DM_WIDTH * 2 * AFI_RATIO - 1:0] afi_dm;
	output [MEM_ODT_WIDTH * AFI_RATIO - 1:0] afi_odt;
	output [MEM_WRITE_DQS_WIDTH * MEM_NUMBER_OF_RANKS * AFI_RATIO - 1:0] afi_wrank;
	output [MEM_READ_DQS_WIDTH * MEM_NUMBER_OF_RANKS * AFI_RATIO - 1:0] afi_rrank;
	input [MEM_DQ_WIDTH * 2 * AFI_RATIO - 1:0] afi_rdata;
	input afi_rdata_valid;

	input cmd_read;
	input cmd_write;
	output cmd_done;
	input [AVL_ADDRESS_WIDTH - 1:0] avl_address;

        input                         csr_clk;       
        input                         csr_ena;       
        input                         csr_dout_phy;      
        output  csr_dout;

	output	[7:0] jump_ptr_0_export; 	
	output  [7:0] jump_ptr_1_export; 
	output  [7:0] jump_cntr_0_export; 	
	output  [7:0] jump_cntr_1_export;
	output  [CS_MASK_WIDTH - 1:0] cs_mask_export;

	reg [AVL_DATA_WIDTH - 1:0] avl_readdata;

	reg di_buffer_clear;

	wire [AC_ROM_FIXED_DATA_WIDTH - 1:0] ac_bus_from_rom;
	wire [AC_ROM_DATA_WIDTH - 1:0] ac_bus_internal;
	wire [GROUP_BUS_SIZE - 1 : 0] DI_mux;
	wire [INST_ROM_DATA_WIDTH - 1:0] opcode;
	wire [INST_ROM_ADDRESS_WIDTH - 1:0] next_PC;
	reg [DI_ADDR_WIDTH-1:0] di_buffer_write_address;
	wire [DEBUG_READ_DI_WIDTH - 1:0] di_buffer_read_data;
	reg [INST_ROM_ADDRESS_WIDTH - 1:0] PC;
	wire [GROUP_COUNTER_WIDTH - 1:0] group_select /* synthesis keep = 1 */;
	wire [JUMP_COUNTER_WIDTH - 1:0] jump_group_counter_ext;
	wire [GROUP_COUNTER_WIDTH - 1:0] jump_group_counter;
	wire [GROUP_BUS_SIZE - 1 : 0] read_datapath_input;
	wire read_datapath_valid;
	reg r_wn_r;
	reg group_mode;
	reg loopback_mode;

	wire [2 * NUMBER_OF_WRITE_DQ_PER_DQS * AFI_RATIO - 1 : 0] do_data;
	wire [NUMBER_OF_WORDS - 1 : 0] dm_data;

	reg [2 * NUMBER_OF_WRITE_DQ_PER_DQS * AFI_RATIO - 1 : 0] do_data_r;
	reg [NUMBER_OF_WORDS - 1 : 0] dm_data_r;

	wire return_code = opcode[19];
	wire r_wn = opcode[18];
	wire DO_lfsr = opcode[17];
	wire DM_lfsr = opcode[16];
	wire jump = opcode[15];
	wire [1:0] jump_reg = opcode[14:13];
	wire [AC_ROM_ADDRESS_WIDTH - 1:0] ac_address = opcode[12:7];
	wire [3:0] do_address = opcode[6:3];
	wire [2:0] dm_address = opcode[2:0];
	wire jump_taken;
	wire [INST_ROM_ADDRESS_WIDTH - 1:0] jump_address;
	wire [7:0] jump_address_jumplogic;
	wire write_DO_lfsr_step = DO_lfsr && !r_wn;
	wire write_DM_lfsr_step = DM_lfsr && !r_wn;
	reg cmd_read_afi;
	reg cmd_write_afi;

	wire [NUMBER_OF_READ_DQ_PER_DQS - 1:0] error_word;

	wire [(INST_ROM_DATA_WIDTH-1):0] inst_ROM_wrdata;
	wire [(INST_ROM_ADDRESS_WIDTH-1):0] inst_ROM_wraddress;
	wire [AC_ROM_FIXED_DATA_WIDTH-1:0] ac_ROM_wrdata_to_rom;
	wire [(AC_ROM_DATA_WIDTH-1):0] ac_ROM_wrdata;
	wire [(AC_ROM_ADDRESS_WIDTH-1):0] ac_ROM_wraddress;

	reg [31:0] di_status_word;

	wire csr_dout = 1'b0;


	reg [AVL_DATA_WIDTH - 1:0] avl_writedata_afi;
	reg [AVL_ADDRESS_WIDTH - 1:0] avl_address_afi;

	always @(posedge afi_clk or negedge afi_reset_n)
	begin
		if (~afi_reset_n)
		begin
		    avl_writedata_afi    <= {AVL_DATA_WIDTH{1'b0}};
		    avl_address_afi      <= {AVL_ADDRESS_WIDTH{1'b0}};
		    cmd_read_afi	 <= 1'b0;
		    cmd_write_afi	 <= 1'b0;
		    r_wn_r               <= 1'b0;
			do_data_r            <= 0;
			dm_data_r            <= 0;
		end
		else begin
		    avl_writedata_afi    <= avl_writedata;
		    avl_address_afi      <= avl_address;
		    cmd_read_afi	 <= cmd_read;
		    cmd_write_afi	 <= cmd_write;
		    r_wn_r               <= r_wn;
			do_data_r            <= do_data;
			dm_data_r            <= dm_data;
		end
	end

	
	assign inst_ROM_wrdata = avl_writedata_afi [(INST_ROM_DATA_WIDTH-1):0];

	assign inst_ROM_wraddress = avl_address_afi[(INST_ROM_ADDRESS_WIDTH-1):0];

	assign ac_ROM_wrdata = avl_writedata_afi[(AC_ROM_DATA_WIDTH-1):0];

	assign ac_ROM_wraddress = avl_address_afi[(AC_ROM_ADDRESS_WIDTH-1):0];

	reg [GROUP_BUS_SIZE - 1 : 0] DI_mux_r;
	reg afi_rdata_valid_r;

	always @(posedge afi_clk or negedge afi_reset_n)
	begin
		if (~afi_reset_n)
		begin
			DI_mux_r			<= {GROUP_BUS_SIZE{1'b0}};
			afi_rdata_valid_r	<= 1'b0;
		end
		else begin
			DI_mux_r			<= DI_mux;
			afi_rdata_valid_r	<= afi_rdata_valid;
		end
	end

	assign read_datapath_input = (loopback_mode) ? do_data: DI_mux_r;
	assign read_datapath_valid = (loopback_mode) ? r_wn_r : afi_rdata_valid_r;
	
	assign ac_bus = ac_bus_internal[AC_BUS_WIDTH - 1:0];


	wire [NUMBER_OF_READ_DQ_PER_DQS - 1:0] processed_error_word;
	
	genvar rank;
	generate
		for(rank = 0; rank < NUMBER_OF_READ_DQ_PER_DQS; rank = rank + 1)
		begin: error_word_gen
			assign processed_error_word[rank] = (error_word[rank] !== 1'b0);
		end
	endgenerate

	always @ (*)
	begin
		di_status_word <= 0;
		if (di_buffer_write_address[DI_ADDR_WIDTH-1:0] > MAX_DI_BUFFER_WORDS)
			di_status_word[15:0] <= MAX_DI_BUFFER_WORDS[15:0];
		else
			di_status_word[DI_ADDR_WIDTH-1:0] <= di_buffer_write_address[DI_ADDR_WIDTH-1:0];
		di_status_word[23:16] <= DEBUG_WRITE_TO_READ_RATIO[7:0];
		di_status_word[31:24] <= DEBUG_WRITE_TO_READ_RATIO_2_EXPONENT[7:0];
	end
	
	always @ (*)
	begin
		if (avl_address_afi == 0)
			avl_readdata <=  {{{32 - NUMBER_OF_READ_DQ_PER_DQS}{1'b0}}, processed_error_word};
		else if (avl_address_afi == 1)
			avl_readdata <= {{{32 - INST_ROM_ADDRESS_WIDTH}{1'b0}}, PC};
		else if (avl_address_afi == 2)
			avl_readdata <= di_status_word;
		else if (avl_address_afi == 3)
			avl_readdata <= di_status_word;
		else
			avl_readdata <= di_buffer_read_data;
	end

	typedef enum int unsigned {
		RW_MGR_STATE_IDLE,
		RW_MGR_STATE_RUNNING,
		RW_MGR_STATE_READING,
		RW_MGR_STATE_DONE
	} RW_MGR_STATE_T;

	RW_MGR_STATE_T state;

	assign cmd_done = (state == RW_MGR_STATE_DONE);

	reg [MEM_NUMBER_OF_RANKS - 1:0] active_rank;
	reg [CS_MASK_WIDTH - 1:0] cs_mask;
	reg [CS_MASK_WIDTH - 1:0] odt_0_mask;
	reg [CS_MASK_WIDTH - 1:0] odt_1_mask;
	reg cmd_reset_r;


	assign cs_mask_export = cs_mask;

	wire [3:0] cmd_opcode = avl_address_afi[11:8];
	wire cmd_run_single_group = (cmd_opcode == 4'b0000) & cmd_write_afi; 
	wire cmd_run_all_groups = (cmd_opcode == 4'b0001) & cmd_write_afi; 

	wire cmd_load_cntr = (cmd_opcode == 4'b0010) & cmd_write_afi; 
	wire cmd_load_jump_address = (cmd_opcode == 4'b0011) & cmd_write_afi; 
	wire cmd_clear_read_datapath = (cmd_opcode == 4'b0100) & cmd_write_afi; 
	wire cmd_set_cs_mask = (cmd_opcode == 4'b0101) & cmd_write_afi; 
	wire inst_ROM_wren = (cmd_opcode == 4'b0110) & cmd_write_afi; 
	wire ac_ROM_wren = (cmd_opcode == 4'b0111) & cmd_write_afi; 
	wire cmd_reset = (cmd_opcode == 4'b1000) & cmd_write_afi; 
	wire cmd_set_active_rank = (cmd_opcode == 4'b1001) & cmd_write_afi; 
	wire cmd_load = (cmd_opcode != 4'b0000 && cmd_opcode != 4'b0001) & cmd_write_afi;
	wire rw_soft_reset_n = afi_reset_n & ~cmd_reset_r /* synthesis keep = 1 */;

	wire [GROUP_COUNTER_WIDTH - 1:0] cmd_run_group = avl_address_afi[GROUP_COUNTER_WIDTH - 1:0];
	wire [INST_ROM_ADDRESS_WIDTH - 1:0] cmd_run_address = avl_writedata_afi[INST_ROM_ADDRESS_WIDTH - 1:0];
	wire [1:0] cmd_reg_select = avl_address_afi[1:0];
	wire cmd_run = cmd_run_single_group | cmd_run_all_groups;
	wire [7:0] cs_mask_setting = avl_writedata_afi[7:0];
	wire [MEM_NUMBER_OF_RANKS-1:0] active_rank_setting = avl_writedata_afi[MEM_NUMBER_OF_RANKS-1:0];
	wire [7:0] odt_0_mask_setting = avl_writedata_afi[15:8];
	wire [7:0] odt_1_mask_setting = avl_writedata_afi[23:16];

	wire option_loopback_mode = avl_address_afi[7];
	wire odt_select = ac_bus_internal[AC_ODT_BIT];
	assign afi_odt = odt_select ?
		{AFI_RATIO{odt_1_mask[MEM_ODT_WIDTH - 1:0]}} : 
		{AFI_RATIO{odt_0_mask[MEM_ODT_WIDTH - 1:0]}};
		
	assign afi_wrank = {AFI_RATIO{ {MEM_WRITE_DQS_WIDTH{active_rank}} }};
	assign afi_rrank = {AFI_RATIO{ {MEM_READ_DQS_WIDTH{active_rank}} }};

	genvar rank2, rate;
	generate
		for(rate = 0; rate < AFI_RATIO; rate = rate + 1)
		begin : rate_counter
			for(rank2 = 0; rank2 < MASK_WIDTH; rank2 = rank2 + 1)
			begin : rank_counter
				if (RATE == "Quarter") begin
					if (USE_ALL_AFI_PHASES_FOR_COMMAND_ISSUE == 0) begin
						if (rate % 2 == 0)
							assign ac_masked_bus[rate * MASK_WIDTH + rank2] = 1'b1;
						else
							assign ac_masked_bus[rate * MASK_WIDTH + rank2] = ac_bus_internal[AC_BUS_WIDTH + rate/2] | cs_mask[rank2];
					end else begin
						if (rate == 0)
							assign ac_masked_bus[rate * MASK_WIDTH + rank2] = ac_bus_internal[AC_BUS_WIDTH + 0] | cs_mask[rank2];
						else
							assign ac_masked_bus[rate * MASK_WIDTH + rank2] = ac_bus_internal[AC_BUS_WIDTH + 1] | cs_mask[rank2];
					end
				end
				else begin
						assign ac_masked_bus[rate * MASK_WIDTH + rank2] = ac_bus_internal[AC_BUS_WIDTH + rate] | cs_mask[rank2];
				end
			end
		end
	endgenerate

	wire [INST_ROM_ADDRESS_WIDTH - 1:0] inst_ROM_address = next_PC;

	
	generate
		begin
			if (HCX_COMPAT_MODE)
				if (DEVICE_FAMILY == "STRATIXIII")
					rw_manager_inst_ROM_hcx_compat_mode_stratixiii #(
						.ROM_INIT_FILE_NAME(INST_ROM_INIT_FILE_NAME)
					)
					inst_ROM_i (
						.data(inst_ROM_wrdata),
						.wraddress(inst_ROM_wraddress),
						.wren(inst_ROM_wren),
						.rdaddress(inst_ROM_address),
						.clock(afi_clk),
						.q(opcode)
					);
				else
					rw_manager_inst_ROM_hcx_compat_mode #(
						.ROM_INIT_FILE_NAME(INST_ROM_INIT_FILE_NAME)
					)
					inst_ROM_i (
						.data(inst_ROM_wrdata),
						.wraddress(inst_ROM_wraddress),
						.wren(inst_ROM_wren),
						.rdaddress(inst_ROM_address),
						.clock(afi_clk),
						.q(opcode)
					);
			else
			    begin
			        if (DEVICE_FAMILY == "MAX10")
				        rw_manager_m10_inst_ROM inst_ROM_i (
				        	.rdaddress(inst_ROM_address),
				        	.clock(afi_clk),
				        	.q(opcode)
				        );
				    else
				        rw_manager_inst_ROM_no_ifdef_params #(
				        	.ROM_INIT_FILE_NAME(INST_ROM_INIT_FILE_NAME)
				        )
				        inst_ROM_i (
				        	.data(inst_ROM_wrdata),
				        	.wraddress(inst_ROM_wraddress),
				        	.wren(inst_ROM_wren),
				        	.rdaddress(inst_ROM_address),
				        	.clock(afi_clk),
				        	.q(opcode)
				        );
				end
		end
	endgenerate

	assign ac_bus_internal = ac_bus_from_rom[AC_ROM_DATA_WIDTH-1:0];
	assign ac_ROM_wrdata_to_rom = ac_ROM_wrdata;
	
	generate
		begin
			if (HCX_COMPAT_MODE)
				rw_manager_ac_ROM_hcx_compat_mode #(
					.ROM_INIT_FILE_NAME(AC_ROM_INIT_FILE_NAME)
				)
				ac_ROM_i (
					.data(ac_ROM_wrdata_to_rom),
					.wraddress(ac_ROM_wraddress),
					.wren(ac_ROM_wren),
					.rdaddress(ac_address),
					.clock(afi_clk),
					.q(ac_bus_from_rom)
				);
			else
			    begin
			        if (DEVICE_FAMILY == "MAX10")
			            rw_manager_m10_ac_ROM ac_ROM_i (
				        	.rdaddress(ac_address),
				        	.clock(afi_clk),
				        	.q(ac_bus_from_rom)
				        );
			        else
				        rw_manager_ac_ROM_no_ifdef_params #(
				        	.ROM_INIT_FILE_NAME(AC_ROM_INIT_FILE_NAME)
				        )
				        ac_ROM_i (
				        	.data(ac_ROM_wrdata_to_rom),
				        	.wraddress(ac_ROM_wraddress),
				        	.wren(ac_ROM_wren),
				        	.rdaddress(ac_address),
				        	.clock(afi_clk),
				        	.q(ac_bus_from_rom)
				        );
				end
		end
	endgenerate

	function [DI_ADDR_WIDTH-1:0] truncate_di_buffer_bits(input[31:0] di_buffer_input);
		return di_buffer_input[DI_ADDR_WIDTH-1:0];
	endfunction

	wire [DI_ADDR_WIDTH-1:0] di_buffer_read_address;
	assign di_buffer_read_address = truncate_di_buffer_bits(avl_address_afi - 4);

	rw_manager_di_buffer_wrap di_buffer_wrap_i(
		.clock(afi_clk),
		.data(read_datapath_input),
		.rdaddress(di_buffer_read_address), 
		.wraddress(di_buffer_write_address),
		.wren(afi_rdata_valid_r),
		.q(di_buffer_read_data),
		.clear(di_buffer_clear));
	defparam di_buffer_wrap_i.DATA_WIDTH = GROUP_BUS_SIZE;
	defparam di_buffer_wrap_i.READ_DATA_SIZE = DEBUG_READ_DI_WIDTH;
	defparam di_buffer_wrap_i.WRITE_TO_READ_RATIO_2_EXPONENT = DEBUG_WRITE_TO_READ_RATIO_2_EXPONENT;
	defparam di_buffer_wrap_i.WRITE_TO_READ_RATIO = DEBUG_WRITE_TO_READ_RATIO;
	defparam di_buffer_wrap_i.ADDR_WIDTH = DI_ADDR_WIDTH;
	defparam di_buffer_wrap_i.NUM_WORDS = MAX_DI_BUFFER_WORDS;

	rw_manager_write_decoder write_decoder_i(
		.ck(afi_clk),
		.reset_n(rw_soft_reset_n),
		.do_lfsr(DO_lfsr),
		.dm_lfsr(DM_lfsr),
		.do_lfsr_step(write_DO_lfsr_step),
		.dm_lfsr_step(write_DM_lfsr_step),
		.do_code(do_address),
		.dm_code(dm_address),
		.do_data(do_data),
		.dm_data(dm_data)
	);
	defparam write_decoder_i.DATA_WIDTH = NUMBER_OF_WRITE_DQ_PER_DQS;
	defparam write_decoder_i.AFI_RATIO = AFI_RATIO;

	wire enable_ap_mode;
	assign enable_ap_mode = |(~cs_mask[1:0] & AP_MODE_EN); 



	rw_manager_read_datapath read_datapath_i(
		.ck(afi_clk),
		.reset_n(rw_soft_reset_n),
		.check_do(do_address),
		.check_dm(dm_address),
		.check_do_lfsr(DO_lfsr),
		.check_dm_lfsr(DM_lfsr),
		.check_pattern_push(r_wn),
		.clear_error(cmd_clear_read_datapath),
		.read_data(read_datapath_input),
		.read_data_valid(read_datapath_valid),
		.error_word(error_word),
		.enable_ap_mode(enable_ap_mode)
	);
	defparam read_datapath_i.DATA_WIDTH = NUMBER_OF_READ_DQ_PER_DQS;
	defparam read_datapath_i.AFI_RATIO = AFI_RATIO;

	rw_manager_data_broadcast data_broadcast_i(
		.dq_data_in(do_data_r),
		.dm_data_in(dm_data_r),
		.dq_data_out(afi_wdata),
		.dm_data_out(afi_dm)
	);
	defparam data_broadcast_i.NUMBER_OF_DQS_GROUPS = MEM_WRITE_DQS_WIDTH;
	defparam data_broadcast_i.NUMBER_OF_DQ_PER_DQS = NUMBER_OF_WRITE_DQ_PER_DQS;
	defparam data_broadcast_i.AFI_RATIO = AFI_RATIO;
	defparam data_broadcast_i.MEM_DM_WIDTH = MEM_DM_WIDTH;


	assign jump_group_counter = jump_group_counter_ext[GROUP_COUNTER_WIDTH - 1:0];
	assign group_select = group_mode ? jump_group_counter : cmd_run_group;

	rw_manager_jumplogic jumplogic_i(
		.ck(afi_clk),
		.reset_n(rw_soft_reset_n),
		.cntr_value(avl_writedata_afi[7:0]),
		.cntr_load(cmd_load_cntr),
		.reg_select(jump_reg),
		.reg_load_select(cmd_reg_select),
		.jump_value(avl_writedata_afi[7:0]),
		.jump_load(cmd_load_jump_address),
		.jump_check(jump),
		.jump_taken(jump_taken),
		.jump_address(jump_address_jumplogic),
		.cntr_3(jump_group_counter_ext),
		.jump_ptr_0_export(jump_ptr_0_export), 	
		.jump_ptr_1_export(jump_ptr_1_export), 
		.jump_cntr_0_export(jump_cntr_0_export), 	
		.jump_cntr_1_export(jump_cntr_1_export) 
	);

	assign jump_address = jump_address_jumplogic[INST_ROM_ADDRESS_WIDTH - 1:0];
		


	genvar w;
	generate
		for(w = 0; w < 2 * AFI_RATIO; w = w + 1)
		begin : mux_iter
			rw_manager_datamux datamux_i(
				.datain(afi_rdata[(w + 1) * MEM_READ_DQS_WIDTH * NUMBER_OF_READ_DQ_PER_DQS - 1 : w * MEM_READ_DQS_WIDTH * NUMBER_OF_READ_DQ_PER_DQS]),
				.sel(group_select),
				.dataout(DI_mux[ (w + 1) * NUMBER_OF_READ_DQ_PER_DQS - 1 : w * NUMBER_OF_READ_DQ_PER_DQS])
			);
			defparam datamux_i.DATA_WIDTH = NUMBER_OF_READ_DQ_PER_DQS;
			defparam datamux_i.SELECT_WIDTH = GROUP_COUNTER_WIDTH;
			defparam datamux_i.NUMBER_OF_CHANNELS = MEM_READ_DQS_WIDTH;
		end
	endgenerate

	assign next_PC = (state == RW_MGR_STATE_IDLE && cmd_run) ? cmd_run_address :
	                      ((jump_taken) ? jump_address : (return_code ? PC : (PC + 1'b1)));

	always @(posedge afi_clk or negedge afi_reset_n) begin
		if(~afi_reset_n) begin
			state <= RW_MGR_STATE_IDLE;
			group_mode <= 1'b0;
			loopback_mode <= 1'b0;
			di_buffer_write_address <= 0;
			PC <= {INST_ROM_ADDRESS_WIDTH{1'b0}};
			cs_mask <= {CS_MASK_WIDTH{1'b0}};			
			odt_0_mask <= {CS_MASK_WIDTH{1'b0}};
			odt_1_mask <= {CS_MASK_WIDTH{1'b0}};
			active_rank <= {MEM_NUMBER_OF_RANKS{1'b0}};
			cmd_reset_r <= 1'b0;
			di_buffer_clear <= 1'b1;
		end
		else begin
			if(~rw_soft_reset_n) begin
				PC <= {INST_ROM_ADDRESS_WIDTH{1'b0}};
			end
			else begin
				PC <= next_PC;
			end

			cmd_reset_r <= cmd_reset;

			di_buffer_clear <= 1'b0;

			if (cmd_clear_read_datapath && state == RW_MGR_STATE_IDLE) begin
				di_buffer_clear <= 1'b1;
				di_buffer_write_address <= 0;
			end
			else if (afi_rdata_valid_r) begin
				di_buffer_write_address <= di_buffer_write_address + 1'b1;
			end

			if(cmd_set_cs_mask) begin
				cs_mask <= cs_mask_setting;
				odt_0_mask <= odt_0_mask_setting[CS_MASK_WIDTH - 1:0];
				odt_1_mask <= odt_1_mask_setting[CS_MASK_WIDTH - 1:0];
			end
			
			if(cmd_set_active_rank) begin
				active_rank <= active_rank_setting;
			end

			case(state)
				RW_MGR_STATE_IDLE : begin
					if(cmd_run) begin
						state <= RW_MGR_STATE_RUNNING;
						group_mode <= cmd_run_all_groups;
						loopback_mode <= option_loopback_mode;
					end
					else if(cmd_read) begin
						state <= RW_MGR_STATE_READING;
					end 
					else if(cmd_load) begin
						state <= RW_MGR_STATE_DONE;
					end 
				end

				RW_MGR_STATE_READING : begin
					state <= RW_MGR_STATE_DONE;
				end

				RW_MGR_STATE_RUNNING : begin
					if(return_code) begin
						state <= RW_MGR_STATE_DONE;
					end
				end

				RW_MGR_STATE_DONE : begin
					if(~cmd_read_afi & ~cmd_write_afi) begin
						state <= RW_MGR_STATE_IDLE;
					end
				end
			endcase
		end
	end

// synthesis translate_off
    assert property (@(posedge (state == RW_MGR_STATE_DONE)) 1);
// synthesis translate_on

endmodule


