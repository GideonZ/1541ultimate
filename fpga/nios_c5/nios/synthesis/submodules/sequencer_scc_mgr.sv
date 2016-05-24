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


// ******
// scc_mgr
// ******
//
// SCC Manager
//
// General Description
// -------------------
//
// This component allows the NIOS to control the delay chains in the IOs.
//

`timescale 1 ps / 1 ps

// altera message_off 10230
module sequencer_scc_mgr (
	// Avalon Interface
	
	avl_clk,
	avl_reset_n,
	avl_address,
	avl_write,
	avl_writedata,
	avl_read,
	avl_readdata,
	avl_waitrequest,

	scc_reset_n,
	scc_clk,
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
	afi_init_req,
	afi_cal_req
);

	parameter AVL_DATA_WIDTH                = 32;
	parameter AVL_ADDR_WIDTH                = 13;

	parameter MEM_IF_READ_DQS_WIDTH	        = 1;
	parameter MEM_IF_WRITE_DQS_WIDTH        = 1;
	parameter MEM_IF_DQ_WIDTH               = 36;
	parameter MEM_IF_DM_WIDTH               = 4;
	parameter MEM_NUMBER_OF_RANKS           = 1;
	
	parameter DLL_DELAY_CHAIN_LENGTH        = 8;
	parameter FAMILY                        = "STRATIXIII";
	parameter USE_2X_DLL			= "false";
	parameter USE_DQS_TRACKING              = 0;
	parameter USE_SHADOW_REGS               = 0;
	parameter DUAL_WRITE_CLOCK              = 0;
	
	parameter TRK_PARALLEL_SCC_LOAD         = 0;
	parameter SCC_DATA_WIDTH                = 1;

	localparam MAX_FAMILY_NAME_LEN          = 30;

	function integer get_dqs_sdata_bits(input [8*MAX_FAMILY_NAME_LEN-1:0] family);
		if (family == "STRATIXV" || family == "ARRIAVGZ") return 101;
		else if (family == "ARRIAV" || family == "CYCLONEV") return 30;
		else return 46;
	endfunction
		
	function integer get_io_sdata_bits(input [8*MAX_FAMILY_NAME_LEN-1:0] family);
		if (family == "STRATIXV" || family == "ARRIAVGZ") return 40;
		else if (family == "ARRIAV" || family == "CYCLONEV") return 25;
		else return 11;
	endfunction
		
	function integer get_datawidth(input [8*MAX_FAMILY_NAME_LEN-1:0] family);
		if (family == "STRATIXV" || family == "ARRIAVGZ") return 42;
		else if (family == "ARRIAV" || family == "CYCLONEV") return 19;
		else return 24;
	endfunction
		

	localparam DQS_IN_PHASE_MAX = 3;
	localparam DQS_SDATA_BITS   = get_dqs_sdata_bits(FAMILY);
	localparam IO_SDATA_BITS    = get_io_sdata_bits(FAMILY);
	localparam DATAWIDTH        = get_datawidth(FAMILY);
	localparam RFILE_LATENCY    = 3;

	localparam MEM_DQ_PER_DQS   = (MEM_IF_DQ_WIDTH / MEM_IF_WRITE_DQS_WIDTH);
	localparam MEM_DM_PER_DQS   = (MEM_IF_DM_WIDTH > MEM_IF_WRITE_DQS_WIDTH) ? (MEM_IF_DM_WIDTH / MEM_IF_WRITE_DQS_WIDTH) : 1;
	localparam MEM_DQS_PER_DM   = (MEM_IF_DM_WIDTH < MEM_IF_WRITE_DQS_WIDTH) ? (MEM_IF_WRITE_DQS_WIDTH / MEM_IF_DM_WIDTH) : 1;

	localparam RFILE_DEPTH      = log2(MEM_DQ_PER_DQS + 1 + MEM_DM_PER_DQS + MEM_IF_READ_DQS_WIDTH - 1) + 1;
	localparam RFILE_ADDR_WIDTH = 6;
	localparam RFILE_USED_DEPTH = (RFILE_DEPTH > RFILE_ADDR_WIDTH) ? RFILE_DEPTH : RFILE_ADDR_WIDTH;
	
	localparam SCC_UPD_WIDTH	= (USE_SHADOW_REGS == 1) ? MEM_IF_READ_DQS_WIDTH : 1;
	localparam SAMPLE_COUNTER_WIDTH = 14;
	
	typedef enum integer {
		SCC_SCAN_DQS		= 'b0000,
		SCC_SCAN_DQS_IO		= 'b0001,
		SCC_SCAN_DQ_IO  	= 'b0010,
		SCC_SCAN_DM_IO		= 'b0011,
		SCC_SCAN_UPD		= 'b1000
	} sdata_scan_t;

	input avl_clk;
	input avl_reset_n;
	input [AVL_ADDR_WIDTH - 1:0] avl_address;
	input avl_write;
	input [AVL_DATA_WIDTH - 1:0] avl_writedata;
	input avl_read;
	output [AVL_DATA_WIDTH - 1:0] avl_readdata;
	output avl_waitrequest;
	
	input scc_clk;
	input scc_reset_n;
	output [SCC_DATA_WIDTH - 1:0] scc_data;
	output [MEM_IF_READ_DQS_WIDTH - 1:0] scc_dqs_ena;
	output [MEM_IF_READ_DQS_WIDTH - 1:0] scc_dqs_io_ena;
	output [MEM_IF_DQ_WIDTH - 1:0] scc_dq_ena;
	output [MEM_IF_DM_WIDTH - 1:0] scc_dm_ena;
	output [SCC_UPD_WIDTH - 1:0] scc_upd;
	
	output [7:0] scc_sr_dqsenable_delayctrl;
	output [7:0] scc_sr_dqsdisablen_delayctrl;
	output [7:0] scc_sr_multirank_delayctrl;

	input [MEM_IF_READ_DQS_WIDTH - 1:0] capture_strobe_tracking;
	input afi_init_req;
	input afi_cal_req;

	// internal versions of interfacing signals.
	
	reg [AVL_DATA_WIDTH - 1:0] avl_readdata;
	reg avl_waitrequest;

	reg [SCC_DATA_WIDTH - 1:0] scc_data;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] scc_dqs_ena;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] scc_dqs_io_ena;
	reg [MEM_IF_DQ_WIDTH - 1:0] scc_dq_ena;
	reg [MEM_IF_DM_WIDTH - 1:0] scc_dm_ena;
	reg [SCC_UPD_WIDTH - 1:0] scc_upd;

	reg scc_data_c;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] scc_dqs_ena_c;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] scc_dqs_io_ena_c;
	reg [MEM_IF_DQ_WIDTH - 1:0] scc_dq_ena_c;
	reg [MEM_IF_DM_WIDTH - 1:0] scc_dm_ena_c;
	reg [SCC_UPD_WIDTH - 1:0] scc_upd_c;	

	// IO config register
	
	reg [IO_SDATA_BITS - 1:0] scc_io_cfg;
	reg [IO_SDATA_BITS - 1:0] scc_io_cfg_curr;
	reg [IO_SDATA_BITS - 1:0] scc_io_cfg_next;
	
	// DQS config register
	
	reg [DQS_SDATA_BITS - 1:0] scc_dqs_cfg;
	reg [DQS_SDATA_BITS - 1:0] scc_dqs_cfg_curr;
	reg [DQS_SDATA_BITS - 1:0] scc_dqs_cfg_next;
	
	logic avl_cmd_parallel_scan;
	logic [RFILE_ADDR_WIDTH-1:0] parallel_rfile_addr;
	logic avl_par_read;
	logic avl_load_done;
	logic avl_load_done_r;
	logic avl_cmd_par_end;
	
	logic scc_load_done;
	logic scc_load_done_r;
	logic avl_par_read_r;
	logic [RFILE_ADDR_WIDTH-1:0] parallel_group;
	logic parallel_cfg_loaded;
	logic [SCC_DATA_WIDTH - 1:0] scc_data_p;
	logic [DQS_SDATA_BITS - 1:0] scc_dqs_cfg_curr_p [SCC_DATA_WIDTH - 1:0];
	logic [DQS_SDATA_BITS - 1:0] scc_dqs_cfg_next_p [SCC_DATA_WIDTH - 1:0];
	logic scc_parallel;
	logic scc_parallel_r;
	
	// is scc manager selected?
	
	reg sel_scc;

	// go signal going to the SCC clock side.
	reg [3:0] scc_go_ena;
	reg [3:0] scc_go_ena_r;
	wire scc_go_group;
	wire scc_go_io;
	wire scc_go_update;
	
	// enable pattern.
	
	reg [7:0] scc_ena_addr;
	reg [255:0] scc_ena_addr_decode;

	// done signal coming back from the scc side.
	
	reg scc_done;
	
	// avalon version of scc done signal
	
	reg avl_done;
	
	// tracking signals
	reg    avl_cmd_trk_afi_end;
	wire   [AVL_DATA_WIDTH - 1:0] read_sample_counter;

	// SCAN state machine

	typedef enum int unsigned {
		STATE_SCC_IDLE,
		STATE_SCC_LOAD,
		STATE_SCC_DONE
	} STATE_SCC_RAM_T;
	
	STATE_SCC_RAM_T scc_state_curr, scc_state_next;
	reg [7:0] scc_shift_cnt_curr;
	reg [7:0] scc_shift_cnt_next;
	
	reg    [DATAWIDTH-1:0]    datain;
	wire    [DATAWIDTH-1:0]    dataout_sr0;
	wire    [DATAWIDTH-1:0]    dataout_sr1;
	wire    [DATAWIDTH-1:0]    dataout;
	wire   [RFILE_ADDR_WIDTH-1:0]    write_addr;
	wire   [RFILE_ADDR_WIDTH-1:0]    read_addr;
	reg    [3:0]    group;
	wire    write_en_sr0;
	wire    write_en_sr1;

	reg [DATAWIDTH-1:0] scc_dataout;

	reg [7:0] group_counter;
	wire avl_cmd_group_counter;
	wire [3:0] avl_cmd_section;
	wire avl_cmd_rfile_group_not_io;
	wire [RFILE_ADDR_WIDTH-1:0] avl_cmd_rfile_addr;

	wire avl_cmd_rank;
	reg [MEM_NUMBER_OF_RANKS - 1:0] avl_active_rank;
	wire avl_active_shadow_reg;
	
	wire avl_cmd_scan;
	wire avl_cmd_scan_begin;
	wire avl_cmd_scan_end;
	wire [RFILE_ADDR_WIDTH-1:0] avl_cmd_scan_addr;

	reg avl_doing_scan;
	reg scc_doing_scan;
	reg scc_doing_scan_r;
	reg [7:0] scc_group_counter;

	wire avl_cmd_rfile;
	wire avl_cmd_rfile_begin;
	wire avl_cmd_rfile_end;
	reg [RFILE_LATENCY-1:0] avl_cmd_rfile_latency;
	
	wire track_opr_check;
	wire avl_cmd_counter_access;

	wire avl_cmd_afi_req;

	wire [AVL_DATA_WIDTH-1:0] shifted_dataout;
	
	// metastability flops
	reg avl_init_req_r;
	reg avl_cal_req_r;
	reg avl_init_req_r2;
	reg avl_cal_req_r2;
	reg avl_init_req_r3;
	reg avl_cal_req_r3;
	
	integer i,j,k,l;
	
	assign sel_scc = 1'b1;
	integer scan_offsets;

	assign avl_cmd_section = avl_address[9:6];
	assign avl_cmd_group_counter = (sel_scc && (avl_cmd_section == 4'b0000));
	assign avl_cmd_rfile_group_not_io = ~(avl_address[9] == 1'b1 || avl_address[9:6] == 4'b0111) | (avl_address[9:6] == 4'b1010);

	assign avl_cmd_rfile = (sel_scc && (avl_address[9:7] != 3'b111) && avl_cmd_section != 4'b0000 && avl_cmd_section != 4'hd);
	assign avl_cmd_rfile_begin = (avl_read || avl_write) && (avl_cmd_rfile || avl_cmd_group_counter) && ~(|avl_cmd_rfile_latency);
	assign avl_cmd_rfile_end = avl_cmd_rfile_latency[0];
	assign avl_cmd_rfile_addr = {'0, (avl_cmd_rfile_group_not_io ? 0 : MEM_IF_READ_DQS_WIDTH) + avl_address[5:0]};

	assign avl_cmd_rank = (sel_scc && avl_cmd_section == 4'he && avl_address[4] == 1'b1);
	assign avl_cmd_parallel_scan = (avl_write && avl_cmd_section == 4'he && avl_address[10] == 1'b1);
	
	assign avl_cmd_scan = (sel_scc && avl_cmd_section == 4'he && ~avl_cmd_rank && ~avl_cmd_parallel_scan);
	assign avl_cmd_scan_begin = (avl_read || avl_write) && avl_cmd_scan && ~(avl_doing_scan) && ~(avl_done);
	assign avl_cmd_scan_end = avl_doing_scan && avl_done;
	assign avl_cmd_scan_addr = {'0, scan_offsets + ((avl_writedata[7:0] == 8'hFF) ? 0 : avl_writedata[5:0])};

	always_comb begin
		case(avl_address[1:0])
			3:  scan_offsets = (MEM_IF_READ_DQS_WIDTH + MEM_DQ_PER_DQS + 1);
			2:  scan_offsets = (MEM_IF_READ_DQS_WIDTH);
			1:  scan_offsets = (MEM_IF_READ_DQS_WIDTH + MEM_DQ_PER_DQS);
			default:  scan_offsets = '0;
		endcase
	end

	assign track_opr_check = (avl_address[5:0] == 6'b111111) ? 1'b1 : 0; 
	assign avl_cmd_counter_access = sel_scc && avl_cmd_section == 4'hF && !track_opr_check;
	assign avl_cmd_afi_req = (sel_scc && avl_cmd_section == 4'hd);
	

	assign avl_waitrequest = (~avl_reset_n) || ((avl_read || avl_write) && ~avl_cmd_rfile_end && ~avl_cmd_scan_end && ~avl_cmd_trk_afi_end && ~avl_cmd_rank && ~avl_cmd_par_end);
	always_comb begin
		if (avl_cmd_rank) begin
			avl_readdata[AVL_DATA_WIDTH - 1:MEM_NUMBER_OF_RANKS] = '0;
			avl_readdata[MEM_NUMBER_OF_RANKS - 1:0] = avl_active_rank;
		end else begin
			if (avl_cmd_counter_access)
				avl_readdata    =    read_sample_counter;
			else
				begin
					if (avl_cmd_afi_req)
						avl_readdata    =    {avl_cal_req_r3,avl_init_req_r3};
					else
						begin
							if (avl_cmd_rfile)
								avl_readdata    =    shifted_dataout;
							else
								avl_readdata    =    group_counter;
						end
				end
		end
	end

	// Assert that the SCC manager only receives broadcast or single bit scan requests for DQS and DM I/Os.
	ERROR_DQS_IO_SCAN_WRONG_DATA:
	assert property (@(posedge avl_clk) (avl_cmd_scan_begin && avl_address[3:0] == SCC_SCAN_DQS_IO) |-> (avl_writedata[7:0] == 8'hFF || avl_writedata[7:0] == 8'h00));
	ERROR_DM_IO_SCAN_WRONG_DATA:
	assert property (@(posedge avl_clk) (avl_cmd_scan_begin && avl_address[3:0] == SCC_SCAN_DM_IO) |-> (avl_writedata[7:0] == 8'hFF || avl_writedata[7:0] < MEM_DM_PER_DQS));
	ERROR_DQS_SCAN_WRONG_DATA:
	assert property (@(posedge avl_clk) (avl_cmd_scan_begin && avl_address[3:0] == SCC_SCAN_DQS) |-> (avl_writedata[7:0] == 8'hFF || avl_writedata[7:0] < MEM_IF_READ_DQS_WIDTH));
	ERROR_DQ_IO_SCAN_WRONG_DATA:
	assert property (@(posedge avl_clk) (avl_cmd_scan_begin && avl_address[3:0] == SCC_SCAN_DQ_IO) |-> (avl_writedata[7:0] == 8'hFF || avl_writedata[7:0] < MEM_DQ_PER_DQS));

		
	typedef bit [13:0] t_setting_mask;
	typedef bit [DATAWIDTH+1:0] t_setting_shifted_mask;

	integer unsigned setting_offsets[1:12];
	t_setting_shifted_mask setting_masks_lshift [1:12];
	t_setting_mask setting_masks [1:12];

	generate
		if (FAMILY == "STRATIXV" || FAMILY == "ARRIAVGZ")
			begin
				assign setting_offsets[1] = 'd0;
				assign setting_offsets[2] = 'd12;
				assign setting_offsets[3] = 'd17;
				assign setting_offsets[4] = 'd25;
				assign setting_offsets[5] = 'd30;
				assign setting_offsets[6] = 'd36;
				assign setting_offsets[7] = 'd0;
				assign setting_offsets[8] = 'd6;
				assign setting_offsets[9] = 'd12;
				assign setting_offsets[10] = 'd0;
				assign setting_offsets[11] = 'd0;
				assign setting_offsets[12] = 'd0;

				assign setting_masks_lshift [1] = 'b0111111111111;
				assign setting_masks_lshift [2] = 'b011111000000000000;
				assign setting_masks_lshift [3] = 'b01111111100000000000000000;
				assign setting_masks_lshift [4] = 'b0111110000000000000000000000000;
				assign setting_masks_lshift [5] = 37'b0111111000000000000000000000000000000;
				assign setting_masks_lshift [6] = 43'b0111111000000000000000000000000000000000000;
				assign setting_masks_lshift [7] = 'b0111111;
				assign setting_masks_lshift [8] = 'b0111111000000;
				assign setting_masks_lshift [9] = 'b0111111111111000000000000;
				assign setting_masks_lshift [10] = 'b0;
				assign setting_masks_lshift [11] = 'b0;
				assign setting_masks_lshift [12] = 'b0;

				assign setting_masks [1] = 'b0111111111111;
				assign setting_masks [2] = 'b011111;
				assign setting_masks [3] = 'b011111111;
				assign setting_masks [4] = 'b011111;
				assign setting_masks [5] = 'b0111111;
				assign setting_masks [6] = 'b0111111;
				assign setting_masks [7] = 'b0111111;
				assign setting_masks [8] = 'b0111111;
				assign setting_masks [9] = 'b0111111111111;
				assign setting_masks [10] = 'b0;
				assign setting_masks [11] = 'b0;
				assign setting_masks [12] = 'b0;
			end 
		else if (FAMILY == "ARRIAV" || FAMILY == "CYCLONEV")
			begin
				assign setting_offsets[1] = 'd0;
				assign setting_offsets[2] = 'd5;
				assign setting_offsets[3] = 'd8;
				assign setting_offsets[4] = 'd13;
				assign setting_offsets[5] = 'd13;
				assign setting_offsets[6] = 'd18;
				assign setting_offsets[7] = 'd0;
				assign setting_offsets[8] = 'd5;
				assign setting_offsets[9] = 'd5;
				assign setting_offsets[10] = 'd18;
				assign setting_offsets[11] = 'd10;
				assign setting_offsets[12] = 'd11;

				assign setting_masks_lshift [1] = 'b011111;
				assign setting_masks_lshift [2] = 'b011100000;
				assign setting_masks_lshift [3] = 'b01111100000000;
				assign setting_masks_lshift [4] = 'b0;
				assign setting_masks_lshift [5] = 'b0111110000000000000;
				assign setting_masks_lshift [6] = 'b0;
				assign setting_masks_lshift [7] = 'b011111;
				assign setting_masks_lshift [8] = 'b0;
				assign setting_masks_lshift [9] = 'b01111100000;
				assign setting_masks_lshift [10] = 'b01000000000000000000;
				assign setting_masks_lshift [11] = 'b010000000000;
				assign setting_masks_lshift [12] = 'b011100000000000;

				assign setting_masks [1] =  'b011111;
				assign setting_masks [2] =  'b0111;
				assign setting_masks [3] =  'b011111;
				assign setting_masks [4] =  'b0;
				assign setting_masks [5] =  'b011111;
				assign setting_masks [6] =  'b0;
				assign setting_masks [7] =  'b011111;
				assign setting_masks [8] =  'b0;
				assign setting_masks [9] =  'b011111;
				assign setting_masks [10] =  'b01;
				assign setting_masks [11] =  'b01;
				assign setting_masks [12] =  'b0111;
			end 
		else
	    begin
				assign setting_offsets[1] = 'd0;
				assign setting_offsets[2] = 'd4;
				assign setting_offsets[3] = 'd8;
				assign setting_offsets[4] = 'd12;
				assign setting_offsets[5] = 'd17;
				assign setting_offsets[6] = 'd21;
				assign setting_offsets[7] = 'd0;
				assign setting_offsets[8] = 'd4;
				assign setting_offsets[9] = 'd7;
				assign setting_offsets[10] = 'd0;
				assign setting_offsets[11] = 'd0;
				assign setting_offsets[12] = 'd0;

				assign setting_masks_lshift [1] = 'b01111;
				assign setting_masks_lshift [2] = 'b011110000;
				assign setting_masks_lshift [3] = 'b0111100000000;
				assign setting_masks_lshift [4] = 'b11111000000000000;
				assign setting_masks_lshift [5] = 'b0111100000000000000000;
				assign setting_masks_lshift [6] = 'b00111000000000000000000000;
				assign setting_masks_lshift [7] = 'b01111;
				assign setting_masks_lshift [8] = 'b001110000;
				assign setting_masks_lshift [9] = 'b011110000000;
				assign setting_masks_lshift [10] = 'b0;
				assign setting_masks_lshift [11] = 'b0;
				assign setting_masks_lshift [12] = 'b0;

				assign setting_masks [1] = 'b01111;
				assign setting_masks [2] = 'b01111;
				assign setting_masks [3] = 'b01111;
				assign setting_masks [4] = 'b11111;
				assign setting_masks [5] = 'b01111;
				assign setting_masks [6] = 'b00111;
				assign setting_masks [7] = 'b01111;
				assign setting_masks [8] = 'b00111;
				assign setting_masks [9] = 'b01111;
				assign setting_masks [10] = 'b0;
				assign setting_masks [11] = 'b0;
				assign setting_masks [12] = 'b0;
	    end 
	endgenerate
	
	always_ff @(posedge avl_clk or negedge avl_reset_n)
	begin
		if (~avl_reset_n)
		begin
			avl_active_rank <= '0;
		end
		else begin
			if (avl_cmd_rank && avl_write) begin
				avl_active_rank <= avl_writedata[MEM_NUMBER_OF_RANKS - 1:0];
			end
		end
	end
	
	always_ff @(posedge avl_clk or negedge avl_reset_n)
	begin
		if (~avl_reset_n)
		begin
			avl_cmd_rfile_latency <= 0;
			avl_doing_scan <= 0;
		end
		else begin
			avl_cmd_rfile_latency <= {avl_cmd_rfile_begin, avl_cmd_rfile_latency[RFILE_LATENCY - 1 : 1]};
			avl_doing_scan <= (avl_cmd_scan_begin || avl_doing_scan) && ~avl_done;
		end
	end
	
	always_ff @(posedge avl_clk or negedge avl_reset_n)
	    begin
	        if (!avl_reset_n)
	            begin
	                avl_cmd_par_end        <=    0;
	                parallel_rfile_addr    <=    0;
	                avl_par_read           <=    0;
	                avl_load_done          <=    0;
	                avl_load_done_r        <=    0;
	            end
	        else
	            begin
	                if (avl_cmd_parallel_scan && !avl_cmd_par_end && avl_done)
	                    avl_cmd_par_end    <=    ~avl_cmd_par_end;
	                else
	                    avl_cmd_par_end    <=    0;
	                
	                avl_load_done          <=    scc_load_done;
	                avl_load_done_r        <=    avl_load_done;
	                
	                if (avl_cmd_parallel_scan)
	                    begin
	                        if (parallel_rfile_addr < SCC_DATA_WIDTH && avl_par_read && avl_load_done_r)
	                            parallel_rfile_addr <= parallel_rfile_addr + 1'b1;
	                    end
	                else
	                    parallel_rfile_addr    <=    0;
	                
	                if (!avl_par_read)
	                    begin
	                        if (avl_cmd_parallel_scan && parallel_rfile_addr < SCC_DATA_WIDTH && !avl_load_done_r)
	                            avl_par_read    <=    ~avl_par_read;
	                    end
	                else
	                    begin
	                        if (avl_load_done_r)
	                            avl_par_read    <=    ~avl_par_read;
	                    end
	            end
	    end

	assign read_addr = avl_cmd_parallel_scan ? parallel_rfile_addr : (avl_cmd_scan ? avl_cmd_scan_addr : avl_cmd_rfile_addr);
	assign write_addr = avl_cmd_rfile_addr;

	assign datain = (dataout & (~setting_masks_lshift[avl_cmd_section])) | ((setting_masks[avl_cmd_section] & avl_writedata) << setting_offsets[avl_cmd_section]);

	assign shifted_dataout = (dataout >> setting_offsets[avl_cmd_section]) & setting_masks[avl_cmd_section];

	// config data storage
	
	
	sequencer_scc_reg_file #(
    .WIDTH  (DATAWIDTH),
		.DEPTH  (RFILE_USED_DEPTH)
	) sequencer_scc_reg_file_inst (
        .clock      (avl_clk    ),
        .data       (datain     ),
        .rdaddress  (read_addr  ),
        .wraddress  (write_addr ),
        .wren       (write_en_sr0),
        .q          (dataout_sr0)
    );
	
	generate
		if (USE_SHADOW_REGS == 1) begin
		
			sequencer_scc_reg_file #(
				.WIDTH  (DATAWIDTH),
				.DEPTH  (RFILE_USED_DEPTH)
			) sequencer_scc_reg_file_inst1 (
				.clock      (avl_clk    ),
				.data       (datain     ),
				.rdaddress  (read_addr  ),
				.wraddress  (write_addr ),
				.wren       (write_en_sr1),
				.q          (dataout_sr1)
			);	
			
			assign avl_active_shadow_reg = | avl_active_rank[MEM_NUMBER_OF_RANKS - 1 : MEM_NUMBER_OF_RANKS / 2];
			
			assign write_en_sr0 = avl_cmd_rfile && avl_write && avl_cmd_rfile_latency[1] && (avl_active_shadow_reg == 1'b0);
			assign write_en_sr1 = avl_cmd_rfile && avl_write && avl_cmd_rfile_latency[1] && (avl_active_shadow_reg == 1'b1);
			assign dataout = (avl_active_shadow_reg == 1'b0 ? dataout_sr0 : dataout_sr1);
			
		end else begin
			assign write_en_sr0 = avl_cmd_rfile && avl_write && avl_cmd_rfile_latency[1];
			assign dataout = dataout_sr0;
		end
	endgenerate 
	
	always_ff @(posedge avl_clk or negedge avl_reset_n)
	begin
		if (~avl_reset_n)
		begin
			group_counter <= '0;
		end
		else begin
			if (avl_cmd_group_counter && avl_write)
			begin
				group_counter <= avl_writedata;
			end
		end
	end
	
	always_ff @(posedge scc_clk or negedge scc_reset_n)
	    begin
	        if (~scc_reset_n)
			begin
	            scc_dataout      <=    0;
				scc_doing_scan   <=    0;
				scc_doing_scan_r <=    0;
			end
	        else begin
	            scc_dataout      <=    dataout;
				scc_doing_scan   <=    avl_doing_scan;
				scc_doing_scan_r <=    scc_doing_scan || (avl_cmd_parallel_scan && parallel_cfg_loaded);
			end
	    end	        
	
	// family specific decoder
	generate
		if (FAMILY == "STRATIXV" || FAMILY == "ARRIAVGZ")
		begin
			sequencer_scc_sv_wrapper # (
				.DATAWIDTH              (DATAWIDTH              ),
				.IO_SDATA_BITS          (IO_SDATA_BITS          ),
				.DQS_SDATA_BITS         (DQS_SDATA_BITS         ),
				.AVL_DATA_WIDTH         (AVL_DATA_WIDTH         ),
				.DLL_DELAY_CHAIN_LENGTH (DLL_DELAY_CHAIN_LENGTH ),
				.DUAL_WRITE_CLOCK	(DUAL_WRITE_CLOCK)
			) sequencer_scc_family_wrapper (
				.reset_n_scc_clk              (scc_reset_n                  ),	
				.scc_clk                      (scc_clk                      ),
				.scc_dataout                  (scc_dataout                  ),
				.scc_io_cfg                   (scc_io_cfg                   ),
				.scc_dqs_cfg                  (scc_dqs_cfg                  ),
				.scc_sr_dqsenable_delayctrl   (scc_sr_dqsenable_delayctrl   ),
				.scc_sr_dqsdisablen_delayctrl (scc_sr_dqsdisablen_delayctrl ),
				.scc_sr_multirank_delayctrl   (scc_sr_multirank_delayctrl   )
			);
	        end
		else if (FAMILY == "ARRIAV" || FAMILY == "CYCLONEV")
		begin
			assign scc_sr_dqsenable_delayctrl = '0;
			assign scc_sr_dqsdisablen_delayctrl = '0;
			assign scc_sr_multirank_delayctrl = '0;
					
			sequencer_scc_acv_wrapper # (
				.DATAWIDTH              (DATAWIDTH              ),
				.IO_SDATA_BITS          (IO_SDATA_BITS          ),
				.DQS_SDATA_BITS         (DQS_SDATA_BITS         ),
				.AVL_DATA_WIDTH         (AVL_DATA_WIDTH         ),
				.DLL_DELAY_CHAIN_LENGTH (DLL_DELAY_CHAIN_LENGTH ),
				.USE_2X_DLL		(USE_2X_DLL)
			) sequencer_scc_family_wrapper (
				.reset_n_scc_clk    (scc_reset_n        ),	
				.scc_clk            (scc_clk            ),
				.scc_dataout        (scc_dataout        ),
				.scc_io_cfg         (scc_io_cfg         ),
				.scc_dqs_cfg        (scc_dqs_cfg        )
			);
	        end
		else
	        begin
			
			assign scc_sr_dqsenable_delayctrl = '0;
			assign scc_sr_dqsdisablen_delayctrl = '0;
			assign scc_sr_multirank_delayctrl = '0;
						
			sequencer_scc_siii_wrapper # (
				.DATAWIDTH              (DATAWIDTH              ),
				.IO_SDATA_BITS          (IO_SDATA_BITS          ),
				.DQS_SDATA_BITS         (DQS_SDATA_BITS         ),
				.AVL_DATA_WIDTH         (AVL_DATA_WIDTH         ),
				.DLL_DELAY_CHAIN_LENGTH (DLL_DELAY_CHAIN_LENGTH )
			) sequencer_scc_family_wrapper (
				.reset_n_scc_clk    (scc_reset_n        ),	
				.scc_clk            (scc_clk            ),
				.scc_dataout        (scc_dataout        ),
				.scc_io_cfg         (scc_io_cfg         ),
				.scc_dqs_cfg        (scc_dqs_cfg        )
			);
		end
	endgenerate
	
	// data transfer from SCC to AVALON
	
	always_ff @ (posedge avl_clk) begin
		avl_done <= scc_done;
	end
	
	// scan chain side state update
	// scan chain state machine transitions.
	
	always_ff @ (posedge scc_clk or negedge scc_reset_n) begin
		if (~scc_reset_n) begin
			scc_go_ena <= '0;
			scc_go_ena_r <= '0;
			scc_ena_addr <= '0;
			scc_io_cfg_curr <= '0;
			scc_dqs_cfg_curr <= '0;
			scc_shift_cnt_curr <= '0;
			scc_state_curr <= STATE_SCC_IDLE;
			scc_group_counter <= '0;
			for (k=0; k<SCC_DATA_WIDTH; k=k+1)
			    scc_dqs_cfg_curr_p[k] <= '0;
		end
		else begin
			scc_go_ena <= avl_address[3:0];
			scc_go_ena_r <= scc_go_ena;
			scc_ena_addr <= avl_writedata[7:0];
			scc_io_cfg_curr <= scc_io_cfg_next;
			scc_dqs_cfg_curr <= scc_dqs_cfg_next;
			scc_shift_cnt_curr <= scc_shift_cnt_next;
			scc_state_curr <= scc_state_next;
			scc_group_counter <= group_counter;
			if (scc_load_done && !scc_load_done_r)
			    scc_dqs_cfg_curr_p[parallel_group] <= scc_dqs_cfg;
			else if (scc_state_curr == STATE_SCC_LOAD)
			    begin
			        for (k=0; k<SCC_DATA_WIDTH; k=k+1)
			            scc_dqs_cfg_curr_p[k]    <=    scc_dqs_cfg_next_p[k];
			    end
		end
	end

	assign scc_go_group = (scc_go_ena_r == SCC_SCAN_DQS);
	assign scc_go_io = (scc_go_ena_r == SCC_SCAN_DQS_IO) || (scc_go_ena_r == SCC_SCAN_DQ_IO) || (scc_go_ena_r == SCC_SCAN_DM_IO);
	assign scc_go_update = (scc_go_ena_r == SCC_SCAN_UPD);

	always_ff @ (posedge scc_clk or negedge scc_reset_n)
	    begin
	        if (!scc_reset_n)
	            begin
	                avl_par_read_r   <=    0;
	                scc_load_done    <=    0;
	                scc_load_done_r  <=    0;
	                parallel_group   <=    0;
	                parallel_cfg_loaded    <=    0;
	            end
	        else
	            begin
	                avl_par_read_r    <=    avl_par_read;
	                scc_load_done_r   <=    scc_load_done;
	                
	                if (!parallel_cfg_loaded)
	                    begin
	                        if (!scc_load_done && scc_load_done_r)
	                            begin
	                                if (parallel_group == SCC_DATA_WIDTH-1)
	                                    parallel_cfg_loaded    <=    1;
	                            end
	                    end
	                else
	                    parallel_cfg_loaded    <=    0;
	                
	                if (!scc_load_done && scc_load_done_r)
	                    begin
	                        if (parallel_group < SCC_DATA_WIDTH-1)
	                            parallel_group    <=    parallel_group + 1'b1;
	                        else
	                            parallel_group    <=    0;
	                    end
	                
	                    scc_load_done    <=    avl_par_read_r;
	            end
	    end
	
	always_ff @ (negedge scc_clk or negedge scc_reset_n)
	    begin
	        if (!scc_reset_n)
	            begin
	                scc_parallel    <=    0;
	                scc_parallel_r  <=    0;
	            end
	        else
	            begin
	                scc_parallel    <=    avl_cmd_parallel_scan;
	                scc_parallel_r  <=    scc_parallel;
	            end
	    end

	always_ff @ (negedge scc_clk) begin
	    if (scc_parallel_r)
	        scc_data <= scc_data_p;
	    else
	        for (l=0; l<SCC_DATA_WIDTH; l=l+1)
	        begin
	            scc_data[l] <= scc_data_c;
	        end
		scc_dqs_ena <= scc_dqs_ena_c;
		scc_dqs_io_ena <= scc_dqs_io_ena_c;
		scc_dq_ena <= scc_dq_ena_c;
		scc_dm_ena <= scc_dm_ena_c;
		scc_upd <= scc_upd_c;
	end

	always_comb begin
		scc_ena_addr_decode = '0;

		if (scc_go_ena_r == SCC_SCAN_DQ_IO)
		begin
			if (scc_ena_addr == 8'b11111111) 
				scc_ena_addr_decode = {MEM_DQ_PER_DQS{1'b1}} << (scc_group_counter * MEM_DQ_PER_DQS);
			else
				scc_ena_addr_decode[scc_group_counter * MEM_DQ_PER_DQS + scc_ena_addr] = 1;
		end
		else if (scc_go_ena_r == SCC_SCAN_DQS) begin
			if (scc_ena_addr == 8'b11111111) 
				scc_ena_addr_decode = '1;
			else
				scc_ena_addr_decode[scc_ena_addr] = 1;
		end
		else if (scc_go_ena_r == SCC_SCAN_DM_IO) begin
			if (scc_ena_addr == 8'b11111111) 
				scc_ena_addr_decode = {MEM_DM_PER_DQS{1'b1}} << ((scc_group_counter * MEM_DM_PER_DQS) >> log2(MEM_DQS_PER_DM));
			else
				scc_ena_addr_decode[(scc_group_counter >> log2(MEM_DQS_PER_DM)) * MEM_DM_PER_DQS + scc_ena_addr] = 1;
		end
		else begin
			if (scc_ena_addr == 8'b11111111) 
				scc_ena_addr_decode = '1;
			else
				scc_ena_addr_decode[scc_group_counter] = 1;
		end
		
		scc_state_next = scc_state_curr;
		scc_shift_cnt_next = '0;
		scc_io_cfg_next = scc_io_cfg;
		scc_dqs_cfg_next = scc_dqs_cfg;
		scc_data_c = 0;
		scc_dqs_ena_c = '0;
		scc_dqs_io_ena_c = '0;
		scc_dq_ena_c = '0;
		scc_dm_ena_c = '0;
		scc_upd_c = '0;
		scc_done = 0;
		scc_data_p = '0;
		for (j=0; j<SCC_DATA_WIDTH; j=j+1)
		    scc_dqs_cfg_next_p[j] = '0;

		case (scc_state_curr)
		STATE_SCC_IDLE: begin
			if (scc_doing_scan_r) begin
				if (scc_go_io) begin
					scc_state_next = STATE_SCC_LOAD;
					scc_shift_cnt_next = IO_SDATA_BITS - 1;
				end else if (scc_go_group) begin
					scc_state_next = STATE_SCC_LOAD;
					scc_shift_cnt_next = DQS_SDATA_BITS - 1;
				end else if (scc_go_update) begin
					scc_state_next = STATE_SCC_DONE;
					if (USE_SHADOW_REGS == 1) begin
						if (scc_group_counter == 8'b11111111) begin
							scc_upd_c = '1;
						end else begin
							scc_upd_c = '0;
							scc_upd_c[scc_group_counter] = 1'b1;
						end
					end else begin
						scc_upd_c = '1;
					end
				end
			end
		end
		STATE_SCC_LOAD: begin
			scc_shift_cnt_next = scc_shift_cnt_curr - 1;

			if (scc_go_group) begin
				scc_dqs_ena_c = (scc_go_ena_r == SCC_SCAN_DQS) ? scc_ena_addr_decode : '0;
				if (FAMILY == "STRATIXV" || FAMILY == "ARRIAVGZ" )
				begin		
					scc_data_c = scc_dqs_cfg_curr[0];
					scc_dqs_cfg_next = scc_dqs_cfg_curr >> 1;
					
					for (j=0; j<SCC_DATA_WIDTH; j=j+1)
					begin
					    scc_data_p[j] = scc_dqs_cfg_curr_p[j][0];
					    scc_dqs_cfg_next_p[j] = scc_dqs_cfg_curr_p[j] >> 1;
					end
				end
				else
				begin
					scc_data_c = scc_dqs_cfg_curr[DQS_SDATA_BITS - 1];
					scc_dqs_cfg_next = scc_dqs_cfg_curr << 1;
					
					for (j=0; j<SCC_DATA_WIDTH; j=j+1)
					begin
					    scc_data_p[j] = scc_dqs_cfg_curr_p[j][DQS_SDATA_BITS - 1];
					    scc_dqs_cfg_next_p[j] = scc_dqs_cfg_curr_p[j] << 1;
					end
				end
			end
			
			if (scc_go_io) begin
				scc_dqs_io_ena_c = (scc_go_ena_r == SCC_SCAN_DQS_IO) ? scc_ena_addr_decode : '0;
				scc_dq_ena_c = (scc_go_ena_r == SCC_SCAN_DQ_IO) ? scc_ena_addr_decode : '0;
				scc_dm_ena_c = (scc_go_ena_r == SCC_SCAN_DM_IO) ? scc_ena_addr_decode : '0;
				if (FAMILY == "STRATIXV" || FAMILY == "ARRIAVGZ")
				begin	
					scc_data_c = scc_io_cfg_curr[0];
					scc_io_cfg_next = scc_io_cfg_curr >> 1;
				end
				else
				begin
					scc_data_c = scc_io_cfg_curr[IO_SDATA_BITS - 1];
					scc_io_cfg_next = scc_io_cfg_curr << 1;
				end
			end 
			
			if (scc_shift_cnt_curr == 0) begin
				scc_state_next = STATE_SCC_DONE;
			end
		end
		STATE_SCC_DONE:	begin
			scc_done = 1;

			if (~scc_doing_scan_r)
				scc_state_next = STATE_SCC_IDLE;
		end
		default : begin end
		endcase
	end
	
	always_ff @(posedge avl_clk, negedge avl_reset_n)
	    begin
	        if (~avl_reset_n)
	            avl_cmd_trk_afi_end    <=    1'b0;
	        else
	            begin
	                if (sel_scc && (avl_cmd_section == 4'hF || avl_cmd_section == 4'hd) && (avl_write || avl_read) && ~avl_cmd_trk_afi_end)
	                    avl_cmd_trk_afi_end    <=    1'b1;
	                else
	                    avl_cmd_trk_afi_end    <=    1'b0;
	            end
	    end

	generate
	wire avl_cmd_do_sample;
	if (USE_DQS_TRACKING == 1)
	    begin
		assign avl_cmd_do_sample = (avl_write && sel_scc && avl_cmd_section == 4'hF && track_opr_check && avl_cmd_trk_afi_end);
       	    end

	if (USE_DQS_TRACKING == 1)
            begin
                reg    [MEM_IF_READ_DQS_WIDTH - 1:0] capture_strobe_tracking_r;
                reg signed [SAMPLE_COUNTER_WIDTH - 1:0] sample_counter [MEM_IF_READ_DQS_WIDTH - 1:0];
                assign read_sample_counter  =   {{(AVL_DATA_WIDTH-SAMPLE_COUNTER_WIDTH){sample_counter[avl_address[5:0]][SAMPLE_COUNTER_WIDTH - 1]}},sample_counter[avl_address[5:0]]};
                
                always_ff @(posedge avl_clk, negedge avl_reset_n)
	                begin
	                    if (~avl_reset_n)
	                        capture_strobe_tracking_r    <=    1'b0;
	                    else
	                        capture_strobe_tracking_r    <=    capture_strobe_tracking;
	                end
	            
	            always_ff @(posedge avl_clk, negedge avl_reset_n)
	                begin
	                    if (~avl_reset_n)
	                        begin
	                            for (i=0; i<MEM_IF_READ_DQS_WIDTH; i=i+1)
	                            begin
	                                sample_counter[i]    <= 1'b0;
	                            end
	                        end
	                    else
	                        begin
	                            for (i=0; i<MEM_IF_READ_DQS_WIDTH; i=i+1)
	                            begin
	                                if (avl_cmd_counter_access && avl_write && i == avl_address[5:0])
	                                    sample_counter[i] <= avl_writedata;
	                                else if (avl_cmd_do_sample && (i == avl_writedata[7:0] || avl_writedata[7:0] == 8'hFF))
	                                    begin
	                                        if (capture_strobe_tracking_r[i])
	                                            begin
	                                                if (!sample_counter[i][SAMPLE_COUNTER_WIDTH-1] && &sample_counter[i][SAMPLE_COUNTER_WIDTH-2:0])
	                                                    sample_counter[i]    <=    sample_counter[i];
	                                                else
	                                                    sample_counter[i]    <=    sample_counter[i] + 1'b1;
	                                            end
	                                        else if (!capture_strobe_tracking_r[i])
	                                            begin
	                                                if (sample_counter[i][SAMPLE_COUNTER_WIDTH-1] && ~(|sample_counter[i][SAMPLE_COUNTER_WIDTH-2:0]))
	                                                    sample_counter[i]    <=    sample_counter[i];
	                                                else
	                                                    sample_counter[i]    <=    sample_counter[i] - 1'b1;
	                                            end
	                                    end
	                            end
	                        end
	                end
	        end
        else
            begin
                assign read_sample_counter  =   '0;
            end
	endgenerate
	
	always_ff @(posedge avl_clk, negedge avl_reset_n)
	    begin
	        if (~avl_reset_n)
	            begin
	                avl_init_req_r    <=    0;
	                avl_cal_req_r     <=    0;
	                avl_init_req_r2   <=    0;
	                avl_cal_req_r2    <=    0;
	                avl_init_req_r3   <=    0;
	                avl_cal_req_r3    <=    0;
	            end
	        else
	            begin
	                avl_init_req_r    <=    afi_init_req;
	                avl_cal_req_r     <=    afi_cal_req;
	                avl_init_req_r2   <=    avl_init_req_r;
	                avl_cal_req_r2    <=    avl_cal_req_r;
	                
	                if (avl_init_req_r2)
	                    avl_init_req_r3   <=    1;
	                else
	                    avl_init_req_r3   <=    0;
	                    
	                if (avl_cal_req_r2)
	                    avl_cal_req_r3    <=    1;
	                else
	                    avl_cal_req_r3    <=    0;
	            end
	    end

	function integer log2;
		input integer value;
		begin
		for (log2=0; value>0; log2=log2+1)
			value = value>>1;
		log2 = log2 - 1;
		end
	endfunction

endmodule
