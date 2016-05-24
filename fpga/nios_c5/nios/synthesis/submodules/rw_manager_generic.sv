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

module rw_manager_generic (
	avl_clk,
	avl_reset_n,
	avl_address,
	avl_write,
	avl_writedata,
	avl_read,
	avl_readdata,
	avl_waitrequest,

	afi_clk,
	afi_reset_n,
	afi_wdata,
	afi_dm,
	afi_odt,
	afi_rdata,
	afi_rdata_valid,
	afi_wrank,
	afi_rrank,

	ac_masked_bus,
	ac_bus,
	csr_clk,
	csr_ena,
	csr_dout_phy,
	csr_dout

);

	parameter AVL_DATA_WIDTH 			= "";
	parameter AVL_ADDRESS_WIDTH			= "";
	
	parameter MEM_DQ_WIDTH				= "";
	parameter MEM_DM_WIDTH				= "";
	parameter MEM_ODT_WIDTH 			= "";
	parameter MEM_NUMBER_OF_RANKS		= "";
	parameter MASK_WIDTH				= "";
	parameter AC_ODT_BIT				= "";
	parameter AC_BUS_WIDTH				= "";
	parameter AC_MASKED_BUS_WIDTH			= "";
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
	parameter AC_ROM_INIT_FILE_NAME = "";
	parameter INST_ROM_INIT_FILE_NAME = "";
	
	parameter USE_ALL_AFI_PHASES_FOR_COMMAND_ISSUE = 0;

	parameter AP_MODE_EN = 2'b00;

	parameter AVL_CLK_PS = 1000;
	parameter AFI_CLK_PS = 1000;

	parameter ENABLE_NON_DES_CAL = 0;
	parameter TREFI = 36000;
	parameter REFRESH_INTERVAL = 30000;
	parameter TRFC = 370;
	parameter REFRESH_RANKS_SIMUL = 1;

	input avl_clk;
	input avl_reset_n;
	input [AVL_ADDRESS_WIDTH-1:0] avl_address;
	input avl_write;
	input [AVL_DATA_WIDTH-1:0] avl_writedata;
	input avl_read;
	output [AVL_DATA_WIDTH-1:0] avl_readdata;
	output avl_waitrequest;

	input afi_clk;
	input afi_reset_n;
	
	output [AC_MASKED_BUS_WIDTH - 1:0] ac_masked_bus;
	output [AC_BUS_WIDTH - 1:0] ac_bus;
	output [MEM_DQ_WIDTH * 2 * AFI_RATIO - 1:0] afi_wdata;
	output [MEM_DM_WIDTH * 2 * AFI_RATIO - 1:0] afi_dm;
	output [MEM_ODT_WIDTH * AFI_RATIO - 1:0] afi_odt;
	output [MEM_WRITE_DQS_WIDTH * MEM_NUMBER_OF_RANKS * AFI_RATIO - 1:0] afi_wrank;
	output [MEM_READ_DQS_WIDTH * MEM_NUMBER_OF_RANKS * AFI_RATIO - 1:0] afi_rrank;

	input [MEM_DQ_WIDTH * 2 * AFI_RATIO - 1:0] afi_rdata;
	input afi_rdata_valid;

        input                         csr_clk;       
        input                         csr_ena;       
        input                         csr_dout_phy;       
        output  csr_dout;



	reg [AVL_DATA_WIDTH-1:0] avl_readdata;
	reg avl_waitrequest;
	
	wire avl_wr;
	reg cmd_done_avl;

	wire [AC_BUS_WIDTH - 1:0] ac_bus;
	wire [AC_MASKED_BUS_WIDTH - 1:0] ac_masked_bus;

	reg [AVL_DATA_WIDTH-1:0] avl_readdata_raw;	

	wire avl_rd;
	wire cmd_read;
	wire cmd_write;
	reg avl_rd_r;
	reg avl_wr_r;


	typedef enum int unsigned {
		STATE_RW_IDLE,
		STATE_RW_EXEC,
		STATE_RW_DONE,
	 	STATE_RW_REFRESH,
		STATE_RW_RESTORE
	} STATE_RW_T;

	STATE_RW_T state;


	assign avl_rd = avl_read;


	wire  [7:0] jump_ptr_0_export; 	
	wire  [7:0] jump_ptr_1_export; 
	wire  [7:0] jump_cntr_0_export; 	
	wire  [7:0] jump_cntr_1_export;
	wire  [7:0] cs_mask_export;

	reg  [7:0] jump_ptr_0_reg; 	
	reg  [7:0] jump_ptr_1_reg; 
	reg  [7:0] jump_cntr_0_reg; 	
	reg  [7:0] jump_cntr_1_reg;
	reg  [7:0] cs_mask_reg;

	reg req_avl_bus;
	reg have_avl_bus;
	wire all_refresh_done;
	wire restore_done;
	reg refresh_enabled;
	reg refresh_took_too_long;

	generate
	if (ENABLE_NON_DES_CAL==1)
	begin
		wire decode_enable_refresh;
		assign decode_enable_refresh = (avl_address [11:8] == 4'b1100);
	
		always @(posedge avl_clk or negedge avl_reset_n)
		begin
			if (~avl_reset_n)
				refresh_enabled <= 1'b0;
			else if (avl_write & decode_enable_refresh)
			begin
				if (avl_writedata == {AVL_DATA_WIDTH{1'b0}})
					refresh_enabled <= 1'b0;
				else
					refresh_enabled <= 1'b1;
			end
		end

		
		assign avl_wr = avl_write & ~decode_enable_refresh;  
		assign avl_readdata = (decode_enable_refresh)?  {'0, refresh_took_too_long}: avl_readdata_raw;

	end
	else
	begin
		assign req_avl_bus = 1'b0;
		assign avl_wr = avl_write;
		assign avl_readdata = avl_readdata_raw;
	end
	endgenerate




	always_ff @(posedge avl_clk) begin
		if (~avl_reset_n) begin
			state <= STATE_RW_IDLE;
			
			cs_mask_reg <= '0;
			jump_ptr_0_reg <= '0; 	
			jump_ptr_1_reg <= '0; 
			jump_cntr_0_reg <= '0; 	
			jump_cntr_1_reg <= '0;
		
		end else begin
			case (state)
			STATE_RW_IDLE:
				begin
				    if (req_avl_bus) begin
					state <= STATE_RW_REFRESH;

					cs_mask_reg <= cs_mask_export;
					jump_ptr_0_reg <= jump_ptr_0_export; 	
					jump_ptr_1_reg <= jump_ptr_1_export; 
					jump_cntr_0_reg <= jump_cntr_0_export; 	
					jump_cntr_1_reg <= jump_cntr_1_export;



			            end else
				    begin		
					    if (avl_rd) begin
						    state <= STATE_RW_EXEC;
					    end
					    if (avl_wr) begin
						    state <= STATE_RW_EXEC;
					    end
				    end
				end
			STATE_RW_EXEC: 
				if (cmd_done_avl) begin
					state <= STATE_RW_DONE;
				end
			STATE_RW_DONE: 
				begin
				    state <= STATE_RW_IDLE;
				end
			STATE_RW_REFRESH:
				begin
					if (all_refresh_done) begin		
						state <= STATE_RW_RESTORE;
					end
				end
			STATE_RW_RESTORE:
				begin	
					if (restore_done) begin
						state <= STATE_RW_DONE;
					end
				end

			endcase
		end
	end


	reg [AVL_DATA_WIDTH - 1:0] avl_writedata_avl;
	reg [AVL_ADDRESS_WIDTH - 1:0] avl_address_avl;
	wire [AVL_DATA_WIDTH - 1:0] avl_readdata_afi;
	wire cmd_done_afi;

	reg [AVL_DATA_WIDTH - 1:0] avl_writedata_avl_refresh;
	reg [AVL_ADDRESS_WIDTH - 1:0] avl_address_avl_refresh;
	wire [AVL_DATA_WIDTH - 1:0] avl_writedata_avl_muxed;
	wire [AVL_ADDRESS_WIDTH - 1:0] avl_address_avl_muxed;

	wire cmd_read_muxed;
	wire cmd_write_muxed;



	generate
	if (ENABLE_NON_DES_CAL==0)
	begin
		assign avl_writedata_avl_muxed = avl_writedata_avl;
		assign avl_address_avl_muxed = avl_address_avl;
		assign cmd_read_muxed = cmd_read;
		assign cmd_write_muxed = cmd_write;
	end
	endgenerate

	reg [AVL_DATA_WIDTH - 1:0] avl_readdata_g_avl;

	always @(posedge avl_clk or negedge avl_reset_n)
	begin
		if (~avl_reset_n)
		begin
		    avl_writedata_avl   <= {AVL_DATA_WIDTH{1'b0}};
		    avl_address_avl     <= {AVL_ADDRESS_WIDTH{1'b0}};
		    avl_readdata_g_avl	<= {AVL_DATA_WIDTH{1'b0}};
		    cmd_done_avl	<= 1'b0;
		    avl_rd_r		<= 1'b0;
		    avl_wr_r		<= 1'b0;
		end
		else begin
		    avl_writedata_avl   <= avl_writedata;
		    avl_address_avl     <= avl_address;
		    avl_readdata_g_avl	<= avl_readdata_afi;
		    cmd_done_avl	<= cmd_done_afi;
		    avl_rd_r		<= avl_rd;
		    avl_wr_r		<= avl_wr;
		end
	end

	rw_manager_core rw_mgr_core_inst (
		.avl_reset_n(avl_reset_n),
		.avl_clk(avl_clk),
		.afi_clk(afi_clk),
		.avl_address(avl_address_avl_muxed),
		.avl_readdata(avl_readdata_afi),
		.avl_writedata(avl_writedata_avl_muxed),
		.afi_reset_n(afi_reset_n),
		.afi_wdata(afi_wdata),
		.afi_dm(afi_dm),
		.afi_odt(afi_odt),
		.afi_rdata(afi_rdata),
		.afi_rdata_valid(afi_rdata_valid),
		.afi_wrank(afi_wrank),
		.afi_rrank(afi_rrank),
		.ac_bus(ac_bus),
		.ac_masked_bus(ac_masked_bus),
		.cmd_read(cmd_read_muxed),
		.cmd_write(cmd_write_muxed),
		.cmd_done(cmd_done_afi),
		.csr_clk(csr_clk),
		.csr_ena(csr_ena),
		.csr_dout_phy(csr_dout_phy),
		.csr_dout(csr_dout),

		.cs_mask_export(cs_mask_export),
		.jump_ptr_0_export(jump_ptr_0_export), 	
		.jump_ptr_1_export(jump_ptr_1_export), 
		.jump_cntr_0_export(jump_cntr_0_export), 	
		.jump_cntr_1_export(jump_cntr_1_export)

	);
	defparam rw_mgr_core_inst.AVL_DATA_WIDTH = AVL_DATA_WIDTH;
	defparam rw_mgr_core_inst.AVL_ADDRESS_WIDTH = AVL_ADDRESS_WIDTH;
	defparam rw_mgr_core_inst.MEM_DQ_WIDTH = MEM_DQ_WIDTH;
	defparam rw_mgr_core_inst.MEM_DM_WIDTH = MEM_DM_WIDTH;
	defparam rw_mgr_core_inst.MEM_ODT_WIDTH = MEM_ODT_WIDTH;
	defparam rw_mgr_core_inst.MEM_NUMBER_OF_RANKS = MEM_NUMBER_OF_RANKS;
	defparam rw_mgr_core_inst.MASK_WIDTH = MASK_WIDTH;
	defparam rw_mgr_core_inst.AC_ODT_BIT = AC_ODT_BIT;
	defparam rw_mgr_core_inst.AC_BUS_WIDTH = AC_BUS_WIDTH;
	defparam rw_mgr_core_inst.AC_MASKED_BUS_WIDTH = AC_MASKED_BUS_WIDTH;
	defparam rw_mgr_core_inst.AFI_RATIO = AFI_RATIO;
	defparam rw_mgr_core_inst.MEM_READ_DQS_WIDTH = MEM_READ_DQS_WIDTH;
	defparam rw_mgr_core_inst.MEM_WRITE_DQS_WIDTH = MEM_WRITE_DQS_WIDTH;
	defparam rw_mgr_core_inst.DEBUG_READ_DI_WIDTH = DEBUG_READ_DI_WIDTH;
	defparam rw_mgr_core_inst.DEBUG_WRITE_TO_READ_RATIO_2_EXPONENT = DEBUG_WRITE_TO_READ_RATIO_2_EXPONENT;
	defparam rw_mgr_core_inst.DEBUG_WRITE_TO_READ_RATIO = DEBUG_WRITE_TO_READ_RATIO;
	defparam rw_mgr_core_inst.RATE = RATE;
	defparam rw_mgr_core_inst.HCX_COMPAT_MODE = HCX_COMPAT_MODE;
	defparam rw_mgr_core_inst.DEVICE_FAMILY = DEVICE_FAMILY;
	defparam rw_mgr_core_inst.AC_ROM_INIT_FILE_NAME = AC_ROM_INIT_FILE_NAME;
	defparam rw_mgr_core_inst.INST_ROM_INIT_FILE_NAME = INST_ROM_INIT_FILE_NAME;
	defparam rw_mgr_core_inst.MAX_DI_BUFFER_WORDS_LOG_2 = MAX_DI_BUFFER_WORDS_LOG_2;
	defparam rw_mgr_core_inst.USE_ALL_AFI_PHASES_FOR_COMMAND_ISSUE = USE_ALL_AFI_PHASES_FOR_COMMAND_ISSUE;
	defparam rw_mgr_core_inst.AP_MODE_EN = AP_MODE_EN;

	assign cmd_read = avl_rd_r & ~cmd_done_avl & (state == STATE_RW_EXEC);
	assign cmd_write = avl_wr_r & ~cmd_done_avl & (state == STATE_RW_EXEC);


	always_comb
	begin
		if (((avl_wr || avl_rd) && (~cmd_done_avl || state != STATE_RW_EXEC)) || ~avl_reset_n || state == STATE_RW_REFRESH) 
			avl_waitrequest = 1;
		else
			avl_waitrequest = 0;
			
		if (avl_rd)
			avl_readdata_raw = avl_readdata_g_avl;
		else
			avl_readdata_raw = '0;
	end
	
    assert property (@(posedge avl_reset_n) AC_MASKED_BUS_WIDTH == MASK_WIDTH*AFI_RATIO)
	    $display("%t, [GENERIC ASSERT] AC_MASKED_BUS_WIDTH PARAMETER is correct", $time);
	else
	    $error("%t, [GENERIC ASSERT] AC_MASKED_BUS_WIDTH PARAMETER is incorrect, AC_MASKED_BUS_WIDTH = %d, MASK_WIDTH*AFI_RATIO = %d", $time, AC_MASKED_BUS_WIDTH, MASK_WIDTH*AFI_RATIO);
	
	assert property (@(posedge avl_clk) !avl_reset_n |-> !cmd_read);
	assert property (@(posedge avl_clk) !avl_reset_n |-> avl_waitrequest);


generate
if (ENABLE_NON_DES_CAL==1)
begin


	localparam MAX_REFI_COUNT= (REFRESH_INTERVAL*1000)/AVL_CLK_PS;
	localparam REALLY_MAX_REFI_COUNT = (TREFI*1000)/AVL_CLK_PS;
	localparam MAX_TRFC = (TRFC*1000)/AFI_CLK_PS;
	localparam WAIT_TIME = 400000/AVL_CLK_PS;   
	reg [16:0] refi_counter;

	reg [2:0] rank_counter;
	wire refresh_done;

	wire time_for_refresh;
	assign time_for_refresh = (ENABLE_NON_DES_CAL==1) & (refi_counter == MAX_REFI_COUNT[16:0]);

	typedef enum int unsigned {
		STATE_RF_IDLE,
		STATE_RF_SET_MASK,
		STATE_RF_PRECHARGE,
		STATE_RF_REFRESH_0,
		STATE_RF_REFRESH_1,
		STATE_RF_REFRESH_2,
		STATE_RF_REFRESH_3,
		STATE_RF_REFRESH_4,
	
		STATE_RF_WAIT,

		STATE_RF_ACTIVATE_0,
		STATE_RF_ACTIVATE_1,
		STATE_RF_ACTIVATE_2,
		STATE_RF_ACTIVATE_3,
		STATE_RF_ACTIVATE_4,

		STATE_RF_RESTORE_0,
		STATE_RF_RESTORE_1,
		STATE_RF_RESTORE_2,
		STATE_RF_RESTORE_3,
		STATE_RF_RESTORE_4


	} STATE_RF_T;

	STATE_RF_T rf_state;

	STATE_RF_T next_rf_state;
	
	wire start_refresh;
	wire issue_refresh_cmd;
	wire go_next_refresh;
	
	wire wait_done;
	reg refresh_cmd;
	reg refresh_wait;


	
	assign issue_refresh_cmd = refresh_cmd;
	assign go_next_refresh = (cmd_done_avl == 1'b0) && (refresh_wait == 1'b1);

	always @(posedge avl_clk or negedge avl_reset_n)
	begin
		if (~avl_reset_n)
		begin
			rf_state <= STATE_RF_IDLE;
			refi_counter <= '0;
			refresh_cmd <= 1'b0;
			refresh_wait <= 1'b0;
			refresh_took_too_long <= 1'b0;

		end
		else
		begin
		
			if (refresh_enabled)
				refi_counter <= (rf_state == STATE_RF_REFRESH_0 || rf_state == STATE_RF_REFRESH_4)? '0 : refi_counter + 1;

			if (refi_counter > REALLY_MAX_REFI_COUNT [16:0])
				refresh_took_too_long <= 1'b1;  

			if (    ((rf_state == STATE_RF_WAIT) & wait_done) |
				((rf_state == STATE_RF_IDLE) & (start_refresh | state == STATE_RW_RESTORE)) |
				(~refresh_wait & cmd_done_avl & (rf_state != STATE_RF_WAIT && rf_state != STATE_RF_IDLE  )) )
			begin
				rf_state <= next_rf_state;
				refresh_cmd <= 1'b0;
				refresh_wait <= 1'b1;
			end
			else if (rf_state != STATE_RF_WAIT & rf_state != STATE_RF_IDLE)
			begin
				if (~cmd_done_avl)
					refresh_wait <= 1'b0;

				if (~refresh_wait)
					refresh_cmd <= 1'b1;
			end

		

		


		end		

	end

	reg [16:0] wait_cntr;
	
	always @(posedge avl_clk or negedge avl_reset_n)
	begin
		if (~avl_reset_n)
			wait_cntr <= '0;
		else
		begin
			if (rf_state == STATE_RF_WAIT)
				wait_cntr <= wait_cntr + 1;
			else
				wait_cntr <= 0;
		end	
	end	

	assign wait_done = (wait_cntr == WAIT_TIME);
	
	
	wire [AVL_DATA_WIDTH-1:0] rank_mask;
	assign rank_mask = (REFRESH_RANKS_SIMUL == 1)? '0 :  {'0, ~(7'h01 << rank_counter)};

	always_comb
	begin
		case (rf_state)
		STATE_RF_IDLE:
			begin
				avl_address_avl_refresh = '0;
				avl_writedata_avl_refresh = '0;
				next_rf_state = (state == STATE_RW_REFRESH & start_refresh)? STATE_RF_SET_MASK : (state == STATE_RW_RESTORE)? STATE_RF_RESTORE_0 : STATE_RF_IDLE;
			end
		STATE_RF_SET_MASK: 
			begin
				avl_address_avl_refresh  = {20'h00000 ,4'b0101, 8'h00};	
				avl_writedata_avl_refresh = rank_mask; 
				next_rf_state = STATE_RF_PRECHARGE;
			end
		STATE_RF_PRECHARGE: 
			begin
				avl_address_avl_refresh = 32'h0000;
				avl_writedata_avl_refresh = 32'h12;
				next_rf_state = STATE_RF_REFRESH_0;
			end
		STATE_RF_REFRESH_0: 
			begin
				avl_address_avl_refresh = 32'h0300;
				avl_writedata_avl_refresh = 32'h14;  
				next_rf_state = STATE_RF_REFRESH_1;
			end
		STATE_RF_REFRESH_1: 
			begin
				avl_address_avl_refresh = 32'h0301; 
				avl_writedata_avl_refresh = 32'h0015; 
				next_rf_state = STATE_RF_REFRESH_2;
			end
		STATE_RF_REFRESH_2: 
			begin
				avl_address_avl_refresh = 32'h0200;  
				avl_writedata_avl_refresh = 32'h0007;  
				next_rf_state = STATE_RF_REFRESH_3;
			end
		STATE_RF_REFRESH_3: 
			begin	
				avl_address_avl_refresh = 32'h0201;   
				avl_writedata_avl_refresh = MAX_TRFC;  
				next_rf_state = STATE_RF_REFRESH_4;
			end
		STATE_RF_REFRESH_4: 
			begin
				avl_address_avl_refresh = 32'h0000;  
				avl_writedata_avl_refresh = 32'h14; 
				next_rf_state = STATE_RF_WAIT;
			end
		STATE_RF_WAIT:
			begin
				avl_address_avl_refresh = 32'h0000;  
				avl_writedata_avl_refresh = 32'd0; 
				next_rf_state = STATE_RF_ACTIVATE_0;
			end
		STATE_RF_ACTIVATE_0:
			begin
				avl_address_avl_refresh = 32'h0200;  
				avl_writedata_avl_refresh = 32'h00;   
				next_rf_state = STATE_RF_ACTIVATE_1;
			end
		STATE_RF_ACTIVATE_1:
			begin
				avl_address_avl_refresh = 32'h0201;  
				avl_writedata_avl_refresh = 32'h0F;
				next_rf_state = STATE_RF_ACTIVATE_2;
			end
		STATE_RF_ACTIVATE_2:
			begin
				avl_address_avl_refresh = 32'h0300;
				avl_writedata_avl_refresh = 32'h0E;
				next_rf_state = STATE_RF_ACTIVATE_3;
			end
		STATE_RF_ACTIVATE_3:
			begin
				avl_address_avl_refresh = 32'h0301;
				avl_writedata_avl_refresh = 32'h10;
				next_rf_state = STATE_RF_ACTIVATE_4;
			end
		STATE_RF_ACTIVATE_4:
			begin
				avl_address_avl_refresh = 32'h0000;
				avl_writedata_avl_refresh = 32'h0D;
				next_rf_state = STATE_RF_IDLE;
			end
		STATE_RF_RESTORE_0:
			begin
				avl_address_avl_refresh = {20'h00000 ,4'b0101, 8'h00}; 
				avl_writedata_avl_refresh = cs_mask_reg;  
				next_rf_state = STATE_RF_RESTORE_1;
			end
		STATE_RF_RESTORE_1:
			begin
				avl_address_avl_refresh = 32'h0200;  
				avl_writedata_avl_refresh = jump_cntr_0_reg;
				next_rf_state = STATE_RF_RESTORE_2;
			end
		STATE_RF_RESTORE_2:
			begin
				avl_address_avl_refresh = 32'h0201;  
				avl_writedata_avl_refresh = jump_cntr_1_reg;
				next_rf_state = STATE_RF_RESTORE_3;
			end
		STATE_RF_RESTORE_3:
			begin
				avl_address_avl_refresh = 32'h0300;
				avl_writedata_avl_refresh = jump_ptr_0_reg; 
				next_rf_state = STATE_RF_RESTORE_4;
			end
		STATE_RF_RESTORE_4:
			begin
				avl_address_avl_refresh = 32'h0301;
				avl_writedata_avl_refresh = jump_ptr_1_reg;
				next_rf_state = STATE_RF_IDLE;
			end
		default:
			begin
				avl_address_avl_refresh = 32'h0000;  
				avl_writedata_avl_refresh = 32'd0; 
				next_rf_state = STATE_RF_IDLE;

			end
		endcase
	end

	assign refresh_done = (rf_state == STATE_RF_ACTIVATE_3);   
	assign restore_done = (rf_state == STATE_RF_RESTORE_4) & cmd_done_avl & ~refresh_wait;  


	always @(posedge avl_clk or negedge avl_reset_n) 
	begin
		if (~avl_reset_n)
			rank_counter <= '0;
		else 
		begin
			if (all_refresh_done | (REFRESH_RANKS_SIMUL == 1))
				rank_counter <= '0;
			else if (refresh_done & go_next_refresh)
				rank_counter <= rank_counter + 1;
		end	
	end


	assign all_refresh_done = ((rank_counter == MEM_NUMBER_OF_RANKS[2:0]) | (REFRESH_RANKS_SIMUL == 1)) & refresh_done;



	reg [7:0] idle_wait;
	localparam IDLE_WAIT_CNT= 32;
	assign start_refresh = (idle_wait == IDLE_WAIT_CNT[7:0]) & have_avl_bus; 
	

	assign avl_writedata_avl_muxed = (have_avl_bus)? avl_writedata_avl_refresh : avl_writedata_avl; 
	assign avl_address_avl_muxed = (have_avl_bus)? avl_address_avl_refresh : avl_address_avl;
	assign cmd_read_muxed = (have_avl_bus)? 1'b0 : cmd_read;
	assign cmd_write_muxed = (have_avl_bus)? issue_refresh_cmd : cmd_write;

	
	always @(posedge avl_clk or negedge avl_reset_n)
	begin
		if (~avl_reset_n)
		begin
			idle_wait <= '0;
			req_avl_bus <= 1'b0;
			have_avl_bus <= 1'b0;
		end
		else
		begin
			if (time_for_refresh)
				req_avl_bus <= 1'b1;
			else if (have_avl_bus)
				req_avl_bus <= 1'b0;

			if (req_avl_bus & state == STATE_RW_REFRESH)
				have_avl_bus <= 1'b1;
			else if (restore_done )
				have_avl_bus <= 1'b0;

			if (have_avl_bus & rf_state == STATE_RF_IDLE)
				idle_wait <= idle_wait +1;
			else
				idle_wait <= '0;

		end
	end
end
endgenerate

endmodule
