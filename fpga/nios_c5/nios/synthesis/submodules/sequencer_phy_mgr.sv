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
// phy_mgr
// ******
//
// PHY Manager
//
// General Description
// -------------------
//
// This component allows a way for a master on the Avalon bus to
// control various aspects of the PHY connection.
//
// Architecture
// ------------
//
// The PHY Manager is organized as follows
//    - Avalon Interface: it's a Memory-Mapped interface to the Avalon
//      Bus.
//    - Register File: it's a set of registers used to control the functioning
//		of the PHY. This register file can be read and written from the Avalon
//      interface and can only be read by the PHY.
//	  - Command Issue: a write to this port runs a particular sequence at PHY
//		speeds. 
//
// Address Space
// -------------
//
// The address space is divided into 2 identical portions.
// The most significant bit select one of the internal components.
// The rest of the address word (whose size depend on parameterization)
// is used to access a specific location of the selected component.
//
// 0 - Instruction/Configuration Address.
//     Data written to this component is intepreted as a command.
//	   Data read from this component returns various parameters specific
//	   to the PHY manager.
//     During read, case (lower address bits)
//			000: returns MAX_LATENCY_COUNT_WIDTH
//			001: returns AFI_MAX_WRITE_LATENCY_COUNT_WIDTH
//			010: returns AFI_MAX_READ_LATENCY_COUNT_WIDTH
//     During write, case (lower address bits)
//			000: increments vfifo_fr specified by word being written
//			001: increments vfifo_hr specified by word being written
//			010: resets the fifo.
//			(the following only used by quarter-rate PHY)
//			011: asserts vfifo_fr and vfifo_hr
//			100: asserts vfifo_qr
//
// 1 - Register File.
//	   8 registers controlling various aspects of the PHY are provided.
//	   The lowest three bits are used to address the specific register
//	   begin read/written from/to.
//	   During reads/writes, case (lower address bits)
//			000: returns/sets read latency counter
//			001: returns/sets mem stable bit.
//

`timescale 1 ps / 1 ps

module sequencer_phy_mgr (
	// Avalon Interface
	
	avl_clk,
	avl_reset_n,
	avl_address,
	avl_write,
	avl_writedata,
	avl_read,
	avl_readdata,
	avl_waitrequest,

	// PHY control

	phy_clk,
	phy_reset_n,
	phy_read_latency_counter,
	phy_read_increment_vfifo_fr,
	phy_read_increment_vfifo_hr,
	phy_read_increment_vfifo_qr,
	phy_reset_mem_stable,
	phy_afi_wlat,
	phy_afi_rlat,
	phy_mux_sel,
	phy_cal_success,
	phy_cal_fail,
	phy_cal_debug_info,
	phy_read_fifo_reset,
	phy_vfifo_rd_en_override,
	phy_read_fifo_q,
	phy_write_fr_cycle_shifts,
	calib_skip_steps
);

	parameter AVL_DATA_WIDTH = 32;
	parameter AVL_ADDR_WIDTH = 13;

	parameter MAX_LATENCY_COUNT_WIDTH = 5;
	parameter MEM_IF_READ_DQS_WIDTH = 1;
	parameter MEM_IF_WRITE_DQS_WIDTH = 1;
	parameter AFI_DQ_WIDTH = 64;
	parameter AFI_DEBUG_INFO_WIDTH = 32;
	parameter AFI_MAX_WRITE_LATENCY_COUNT_WIDTH = 5;
	parameter AFI_MAX_READ_LATENCY_COUNT_WIDTH	= 5;
	parameter CALIB_VFIFO_OFFSET = 10;
	parameter CALIB_LFIFO_OFFSET = 3;
	parameter CALIB_REG_WIDTH = 8;
	parameter READ_VALID_FIFO_SIZE = 16;
	parameter MEM_T_WL = 1;
	parameter MEM_T_RL = 2;
	parameter CTL_REGDIMM_ENABLED = 0;
	parameter NUM_WRITE_FR_CYCLE_SHIFTS = 0;
	parameter DEVICE_FAMILY = "";
	parameter VFIFO_CONTROL_WIDTH_PER_DQS = 1;
	
	localparam  WRITE_FR_CYCLE_SHIFT_WIDTH = 2*MEM_IF_WRITE_DQS_WIDTH;
	localparam  AFI_GROUP_WRITE_LATENCY_COUNT_WIDTH = (AFI_MAX_WRITE_LATENCY_COUNT_WIDTH <= 6) ? AFI_MAX_WRITE_LATENCY_COUNT_WIDTH : AFI_MAX_WRITE_LATENCY_COUNT_WIDTH/MEM_IF_WRITE_DQS_WIDTH;
	
	input avl_clk;
	input avl_reset_n;
	input [AVL_ADDR_WIDTH - 1:0] avl_address;
	input avl_write;
	input [AVL_DATA_WIDTH - 1:0] avl_writedata;
	input avl_read;
	output [AVL_DATA_WIDTH - 1:0] avl_readdata;
	output avl_waitrequest;

	input phy_clk;
	input phy_reset_n;
	output [MAX_LATENCY_COUNT_WIDTH - 1:0] phy_read_latency_counter;
	output [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_increment_vfifo_fr;
	output [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_increment_vfifo_hr;
	output [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_increment_vfifo_qr;
	output phy_reset_mem_stable;
	output [AFI_MAX_WRITE_LATENCY_COUNT_WIDTH - 1:0] phy_afi_wlat;
	output [AFI_MAX_READ_LATENCY_COUNT_WIDTH - 1:0] phy_afi_rlat;
	output [WRITE_FR_CYCLE_SHIFT_WIDTH - 1:0] phy_write_fr_cycle_shifts;
	output phy_mux_sel;
	output phy_cal_success;
	output phy_cal_fail;
	output [AFI_DEBUG_INFO_WIDTH - 1:0] phy_cal_debug_info;
	output [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_fifo_reset;
	output [MEM_IF_READ_DQS_WIDTH - 1:0] phy_vfifo_rd_en_override;
	input  [AFI_DQ_WIDTH-1:0] phy_read_fifo_q;

	input [CALIB_REG_WIDTH - 1:0] calib_skip_steps;
	
	reg avl_waitrequest;
	reg [AVL_DATA_WIDTH - 1:0] avl_readdata_r;
	reg [AVL_DATA_WIDTH - 1:0] avl_readdata;

	reg avl_rfile_wr;
	reg avl_issue_wr;
	reg [VFIFO_CONTROL_WIDTH_PER_DQS*MEM_IF_READ_DQS_WIDTH - 1:0] avl_read_increment_vfifo_fr;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] avl_read_increment_vfifo_hr;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] avl_read_increment_vfifo_qr;	
	reg avl_read_fifo_reset;
	
	reg [MAX_LATENCY_COUNT_WIDTH - 1:0] phy_read_latency_counter;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_increment_vfifo_fr;
	reg [VFIFO_CONTROL_WIDTH_PER_DQS*MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_increment_vfifo_fr_pre_combined /* synthesis preserve = 1 */;
	wire [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_increment_vfifo_fr_combined /* synthesis keep = 1 */;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_increment_vfifo_hr;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_increment_vfifo_qr;	
	reg phy_reset_mem_stable;
	reg [AFI_MAX_WRITE_LATENCY_COUNT_WIDTH - 1:0] phy_afi_wlat;
	reg [AFI_MAX_READ_LATENCY_COUNT_WIDTH - 1:0] phy_afi_rlat;
	reg [WRITE_FR_CYCLE_SHIFT_WIDTH - 1:0] phy_write_fr_cycle_shifts;
	reg phy_mux_sel;
	reg phy_cal_success;
	reg phy_cal_fail;
	reg [AFI_DEBUG_INFO_WIDTH - 1:0] phy_cal_debug_info;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] phy_read_fifo_reset;
	reg [MEM_IF_READ_DQS_WIDTH - 1:0] phy_vfifo_rd_en_override;
	reg phy_done;
	
	wire [32 - 1:0] num_fr_cycle_shifts;
	assign num_fr_cycle_shifts = NUM_WRITE_FR_CYCLE_SHIFTS;
	
	function integer ceil_log2;
		input integer value;
		begin
			value = value - 1;
			for (ceil_log2 = 0; value > 0; ceil_log2 = ceil_log2 + 1)
				value = value >> 1;
		end
	endfunction	
	
	localparam RFILE_ADDR_WIDTH = (NUM_WRITE_FR_CYCLE_SHIFTS == 0) ? 3 : ((AFI_MAX_WRITE_LATENCY_COUNT_WIDTH <= 6) ? ceil_log2(7 + MEM_IF_WRITE_DQS_WIDTH + 1) : ceil_log2(7 + 2*MEM_IF_WRITE_DQS_WIDTH + 1));
	
	integer i;

	typedef enum int unsigned {
		STATE_AVL_IDLE,
		STATE_AVL_EXEC,
		STATE_AVL_DONE
	} STATE_AVL_T;

	STATE_AVL_T state_avl_curr;

	typedef enum int unsigned {
		STATE_PHY_IDLE,
		STATE_PHY_DONE
	} STATE_PHY_T;

	STATE_PHY_T state_phy_curr;

	// the register file
	
	reg [AVL_DATA_WIDTH - 1:0] rfile [0:(NUM_WRITE_FR_CYCLE_SHIFTS == 0) ? 7 : ((AFI_MAX_WRITE_LATENCY_COUNT_WIDTH <= 6) ? 7 + MEM_IF_WRITE_DQS_WIDTH : 7+2*MEM_IF_WRITE_DQS_WIDTH)];
	
	// register file selected
	
	wire sel_rfile, sel_rfile_wr, sel_rfile_rd;
	
	// command issue selected
	
	wire sel_any;
	wire sel_issue, sel_issue_wr, sel_issue_rd;
	
	assign sel_rfile = avl_address[AVL_ADDR_WIDTH - 1];
	assign sel_rfile_wr = sel_rfile & avl_write;
	assign sel_rfile_rd = sel_rfile & avl_read;
	
	assign sel_issue = ~avl_address[AVL_ADDR_WIDTH - 1];
	assign sel_issue_wr = sel_issue & avl_write;
	assign sel_issue_rd = sel_issue & avl_read;
	
	assign sel_any = sel_rfile_wr | sel_rfile_rd | sel_issue_wr | sel_issue_rd;
	
	// State machine, AVALON side
		
	always_ff @ (posedge avl_clk or negedge avl_reset_n) begin
		integer reg_file_index;
		if (avl_reset_n == 0) begin
			state_avl_curr <= STATE_AVL_IDLE;

			avl_readdata_r <= '0;
			avl_rfile_wr <= 0;
			avl_issue_wr <= 0;
			avl_read_increment_vfifo_fr <= '0;
			avl_read_increment_vfifo_hr <= '0;
			avl_read_increment_vfifo_qr <= '0;
			avl_read_fifo_reset <= 0;

			rfile[0][MAX_LATENCY_COUNT_WIDTH - 1:0] <= '0;
			rfile[1][0] <= 0;
			rfile[2][0] <= 1;
			rfile[3][1:0] <= '0;
			rfile[4] <= '0;
			rfile[5][0] <= 0;
			rfile[6] <= 0;
			rfile[7] <= 0;
			if (NUM_WRITE_FR_CYCLE_SHIFTS == -1) begin
				for (reg_file_index = 8; reg_file_index < 8 + MEM_IF_WRITE_DQS_WIDTH; reg_file_index++) begin
					rfile[reg_file_index] <= '0;
				end
				if (AFI_MAX_WRITE_LATENCY_COUNT_WIDTH > 6) begin
					for (reg_file_index = 8 + MEM_IF_WRITE_DQS_WIDTH; reg_file_index < 8 + 2*MEM_IF_WRITE_DQS_WIDTH; reg_file_index++) begin
						rfile[reg_file_index] <= '0;
					end
				end
			end 
		end else begin
			case (state_avl_curr)
			STATE_AVL_IDLE: begin
				if (sel_rfile_rd) begin
					// NIOS is reading from the register file
					
					state_avl_curr <= STATE_AVL_DONE;
					
					avl_readdata_r <= '0 /*rfile[avl_address[RFILE_ADDR_WIDTH - 1:0]] */; // removing address file access for further optimization.
				end else if (sel_issue_rd) begin
					// NIOS is reading parameters

					state_avl_curr <= STATE_AVL_DONE;
					
					case (avl_address[3:0])
					4'b0000: avl_readdata_r <= MAX_LATENCY_COUNT_WIDTH;
					4'b0001: avl_readdata_r <= AFI_MAX_WRITE_LATENCY_COUNT_WIDTH;
					4'b0010: avl_readdata_r <= AFI_MAX_READ_LATENCY_COUNT_WIDTH;
					`ifndef SYNTH_FOR_SIM
					//synthesis translate_off
					`endif
					// extra bit indicates we are in the simulator
					4'b0011: avl_readdata_r <= {{(AVL_DATA_WIDTH-CALIB_REG_WIDTH-1){1'b0}},1'b1,calib_skip_steps};
					`ifndef SYNTH_FOR_SIM
					//synthesis translate_on
					`endif
					`ifndef SYNTH_FOR_SIM
					//synthesis read_comments_as_HDL on
					`endif
					`ifndef SYNTH_FOR_SIM
					// synthesis read_comments_as_HDL off
					`endif
					// 4'b0011: avl_readdata_r <= {{(AVL_DATA_WIDTH-CALIB_REG_WIDTH){1'b0}},calib_skip_steps};
					4'b0100: avl_readdata_r <= CALIB_VFIFO_OFFSET;
					4'b0101: avl_readdata_r <= CALIB_LFIFO_OFFSET;
					4'b0110:
						if (CTL_REGDIMM_ENABLED == 1)
							avl_readdata_r <= 1;
						else
							avl_readdata_r <= 0;
					4'b0111: avl_readdata_r <= MEM_T_WL;
					4'b1000: avl_readdata_r <= MEM_T_RL;
					4'b1001: avl_readdata_r <= READ_VALID_FIFO_SIZE;
					endcase
				end else if (sel_rfile_wr) begin
					// NIOS is writing into the register file.
					// Need to issue a command the PHY side to pick up changes.

					state_avl_curr <= STATE_AVL_EXEC;

					avl_rfile_wr <= 1;
					
					rfile[avl_address[RFILE_ADDR_WIDTH - 1:0]] <= avl_writedata;
				end else if (sel_issue_wr) begin
					// NIOS is asking for a particular command to be issued
					
					state_avl_curr <= STATE_AVL_EXEC;
					
					avl_issue_wr <= 1;
					
					case (avl_address[2:0])
					3'b000:
						if (avl_writedata == 8'b11111111) begin
							avl_read_increment_vfifo_fr <= {(MEM_IF_READ_DQS_WIDTH*VFIFO_CONTROL_WIDTH_PER_DQS){1'b1}};
						end else begin
							avl_read_increment_vfifo_fr[avl_writedata] <= 1;
						end
					3'b001:
						if (avl_writedata == 8'b11111111) begin
							avl_read_increment_vfifo_hr <= {MEM_IF_READ_DQS_WIDTH{1'b1}};
						end else begin
							avl_read_increment_vfifo_hr[avl_writedata] <= 1;
						end
					3'b010:
						avl_read_fifo_reset <= 1;
					3'b011:
						if (avl_writedata == 8'b11111111) begin
							avl_read_increment_vfifo_fr <= {(MEM_IF_READ_DQS_WIDTH*VFIFO_CONTROL_WIDTH_PER_DQS){1'b1}};
							avl_read_increment_vfifo_hr <= {MEM_IF_READ_DQS_WIDTH{1'b1}};
						end else begin
							avl_read_increment_vfifo_fr[avl_writedata] <= 1;
							avl_read_increment_vfifo_hr[avl_writedata] <= 1;
						end
					3'b100:
						if (avl_writedata == 8'b11111111) begin
							avl_read_increment_vfifo_qr <= {MEM_IF_READ_DQS_WIDTH{1'b1}};
						end else begin
							avl_read_increment_vfifo_qr[avl_writedata] <= 1;
						end
					endcase
				end
			end
			STATE_AVL_EXEC: begin
				// We are currently executing a command on the PHY side.
				// Wait until it is done.
				
				if (phy_done) begin
					state_avl_curr <= STATE_AVL_DONE;
					
					// Need to bring all the command lines down to prevent
					// the PHY from reexecuting the same command.
					
					avl_rfile_wr <= 0;
					avl_issue_wr <= 0;
					avl_read_increment_vfifo_fr <= '0;
					avl_read_increment_vfifo_hr <= '0;
					avl_read_increment_vfifo_qr <= '0;
					avl_read_fifo_reset <= 0;
				end
			end
			STATE_AVL_DONE: begin
				// Done operation. During this clock waitrequest is de-asserted.
				// Return to idle state next clock.
				
				state_avl_curr <= STATE_AVL_IDLE;
			end
			endcase
		end
	end

	// State machine, PHY side
		
	always_ff @ (posedge phy_clk or negedge phy_reset_n) begin
		integer reg_file_index;
		integer offset;
		if (phy_reset_n == 0) begin
			state_phy_curr <= STATE_PHY_IDLE;

			phy_done <= 0;
			phy_read_increment_vfifo_fr_pre_combined <= '0;
			phy_read_increment_vfifo_hr <= '0;
			phy_read_increment_vfifo_qr <= '0;
			phy_read_fifo_reset <= '0;
			phy_reset_mem_stable <= 0;
			phy_mux_sel <= 1;
			phy_cal_success <= 0;
			phy_cal_fail <= 0;
			phy_cal_debug_info <= '0;
			phy_vfifo_rd_en_override <= '0;
			phy_write_fr_cycle_shifts <= '0;
			phy_afi_rlat <= '0;	
			phy_afi_wlat <= '0;
			phy_read_latency_counter <= '0;
		end else begin
			case (state_phy_curr)
			STATE_PHY_IDLE: begin
				// Waiting for a command to arrive.
				
				if (avl_rfile_wr) begin
					// Register file was just changed on the AVL side. Pick
					// up changes.
					
					state_phy_curr <= STATE_PHY_DONE;
					
					phy_done <= 1;
					
					phy_read_latency_counter <= rfile[0][MAX_LATENCY_COUNT_WIDTH - 1:0];
					phy_reset_mem_stable <= rfile[1][0];
					phy_mux_sel <= rfile[2][0];
					phy_cal_success <= rfile[3][0];
					phy_cal_fail <= rfile[3][1];
					phy_cal_debug_info <= rfile[4][AFI_DEBUG_INFO_WIDTH - 1:0];
					phy_vfifo_rd_en_override <= {(MEM_IF_READ_DQS_WIDTH){rfile[5][0]}};
					if (NUM_WRITE_FR_CYCLE_SHIFTS == -1) begin
						for (reg_file_index = 8; reg_file_index < 8 + MEM_IF_WRITE_DQS_WIDTH; reg_file_index++) begin
							offset = (reg_file_index - 8)*WRITE_FR_CYCLE_SHIFT_WIDTH/MEM_IF_WRITE_DQS_WIDTH;
							phy_write_fr_cycle_shifts[offset+:WRITE_FR_CYCLE_SHIFT_WIDTH/MEM_IF_WRITE_DQS_WIDTH] <= rfile[reg_file_index][WRITE_FR_CYCLE_SHIFT_WIDTH/MEM_IF_WRITE_DQS_WIDTH - 1:0];
						end
						if (AFI_MAX_WRITE_LATENCY_COUNT_WIDTH > 6) begin
							for (reg_file_index = 8 + MEM_IF_WRITE_DQS_WIDTH; reg_file_index < 8 + 2*MEM_IF_WRITE_DQS_WIDTH; reg_file_index++) begin
								offset = (reg_file_index - 8 - MEM_IF_WRITE_DQS_WIDTH)*AFI_GROUP_WRITE_LATENCY_COUNT_WIDTH;
								phy_afi_wlat[offset+:AFI_GROUP_WRITE_LATENCY_COUNT_WIDTH] <= rfile[reg_file_index][AFI_GROUP_WRITE_LATENCY_COUNT_WIDTH - 1:0];
							end
						end else begin
							phy_afi_wlat <= rfile[6][AFI_MAX_WRITE_LATENCY_COUNT_WIDTH - 1:0];
						end
					end else begin
						for (reg_file_index = 0; reg_file_index < MEM_IF_WRITE_DQS_WIDTH; reg_file_index++) begin
							offset = reg_file_index*WRITE_FR_CYCLE_SHIFT_WIDTH/MEM_IF_WRITE_DQS_WIDTH;
							phy_write_fr_cycle_shifts[offset+:WRITE_FR_CYCLE_SHIFT_WIDTH/MEM_IF_WRITE_DQS_WIDTH] <= num_fr_cycle_shifts[1:0];
						end
						phy_afi_wlat <= rfile[6][AFI_MAX_WRITE_LATENCY_COUNT_WIDTH - 1:0];
					end
					phy_afi_rlat <= rfile[7][AFI_MAX_READ_LATENCY_COUNT_WIDTH - 1:0];
				end else if (avl_issue_wr) begin
					// NIOS just issued a command to be run.
					
					state_phy_curr <= STATE_PHY_DONE;
					
					phy_done <= 1;
					
					phy_read_increment_vfifo_fr_pre_combined <= avl_read_increment_vfifo_fr;
					phy_read_increment_vfifo_hr <= avl_read_increment_vfifo_hr;
					phy_read_increment_vfifo_qr <= avl_read_increment_vfifo_qr;
					phy_read_fifo_reset <= {(MEM_IF_READ_DQS_WIDTH){avl_read_fifo_reset}};
				end
			end
			STATE_PHY_DONE: begin
				phy_read_increment_vfifo_fr_pre_combined <= '0;
				phy_read_increment_vfifo_hr <= '0;
				phy_read_increment_vfifo_qr <= '0;
				phy_read_fifo_reset <= '0;

				if (~avl_rfile_wr && ~avl_issue_wr) begin
					state_phy_curr <= STATE_PHY_IDLE;
					phy_done <= 0;
				end
			end
			endcase
		end
	end
	
	// Do not modify the following logic. This is used by the fitter to identify VFIFO control structures in Arria V
	generate
	if (DEVICE_FAMILY == "ARRIAV" && VFIFO_CONTROL_WIDTH_PER_DQS != 1)
	begin
		genvar dqs_index;
		for (dqs_index=0; dqs_index < MEM_IF_READ_DQS_WIDTH; dqs_index=dqs_index+1)
		begin : vfifo_fb56390_gen
			wire [5:0] phy_read_increment_vfifo_fr_pre_combined_ext;
			assign phy_read_increment_vfifo_fr_pre_combined_ext = {2'b00, phy_read_increment_vfifo_fr_pre_combined[(dqs_index*VFIFO_CONTROL_WIDTH_PER_DQS+VFIFO_CONTROL_WIDTH_PER_DQS-1):dqs_index*VFIFO_CONTROL_WIDTH_PER_DQS]};
			// synthesis read_comments_as_HDL on
			// arriav_lcell_comb vfifo_fb56390
			// (
				// .dataa(phy_read_increment_vfifo_fr_pre_combined_ext[0]),
				// .datab(phy_read_increment_vfifo_fr_pre_combined_ext[1]),
				// .datac(phy_read_increment_vfifo_fr_pre_combined_ext[2]),
				// .datad(phy_read_increment_vfifo_fr_pre_combined_ext[3]),
				// .datae(phy_read_increment_vfifo_fr_pre_combined_ext[4]),
				// .dataf(phy_read_increment_vfifo_fr_pre_combined_ext[5]),
				// .combout(phy_read_increment_vfifo_fr_combined[dqs_index])
			// );
			// defparam vfifo_fb56390.lut_mask = 64'h000000000000FFFE;
			// synthesis read_comments_as_HDL off
			// synthesis translate_off
			assign phy_read_increment_vfifo_fr_combined[dqs_index] = phy_read_increment_vfifo_fr_pre_combined[dqs_index*VFIFO_CONTROL_WIDTH_PER_DQS];
			// synthesis translate_on
		end
	end
	else
	begin
		assign phy_read_increment_vfifo_fr_combined = phy_read_increment_vfifo_fr_pre_combined;
	end
	endgenerate
	assign phy_read_increment_vfifo_fr = phy_read_increment_vfifo_fr_combined;

	// wait request management and read data gating
	
	always_comb
	begin
		if (sel_any && state_avl_curr != STATE_AVL_DONE || ~avl_reset_n)
			avl_waitrequest <= 1;
		else 
			avl_waitrequest <= 0;

		if (sel_rfile_rd || sel_issue_rd)
			avl_readdata <= avl_readdata_r;
		else 
			avl_readdata <= '0;
	end
	
endmodule
