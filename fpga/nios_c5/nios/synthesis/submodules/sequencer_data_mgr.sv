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
// data_mgr
// ******
//
// Data Manager
//
// General Description
// -------------------
//
// This component stores all configuration/parameterization information
// that other components need to calibrate.
//
// Architecture
// ------------
//
// The PHY Manager is organized as an 
//    - Avalon Interface: it's a Memory-Mapped interface to the Avalon
//      Bus.
//    - Register File: The "register file" isn't really a register file, and
//		returns values passed in as params to the Avalon Master. It is read-only
//

`timescale 1 ps / 1 ps

module sequencer_data_mgr (
	// Avalon Interface
	
	avl_clk,
	avl_reset_n,
	avl_address,
	avl_write,
	avl_writedata,
	avl_read,
	avl_readdata,
	avl_waitrequest
);

	parameter AVL_DATA_WIDTH = 32;
	parameter AVL_ADDR_WIDTH = 13;

	parameter MAX_LATENCY_COUNT_WIDTH = 5;
	parameter MEM_READ_DQS_WIDTH = 1;
	parameter AFI_DEBUG_INFO_WIDTH = 32;
	parameter AFI_MAX_WRITE_LATENCY_COUNT_WIDTH = 5;
	parameter AFI_MAX_READ_LATENCY_COUNT_WIDTH	= 5;
	parameter CALIB_VFIFO_OFFSET = 10;
	parameter CALIB_LFIFO_OFFSET = 3;
	parameter CALIB_SKIP_STEPS_WIDTH = 8;
	parameter READ_VALID_FIFO_SIZE = 16;
	parameter MEM_T_WL = 1;
	parameter MEM_T_RL = 2;
	parameter CTL_REGDIMM_ENABLED = 0;
	parameter SEQUENCER_VERSION = 0;

	localparam SIGNATURE = 32'h0000002B;

	input avl_clk;
	input avl_reset_n;
	input [AVL_ADDR_WIDTH - 1:0] avl_address;
	input avl_write;
	input [AVL_DATA_WIDTH - 1:0] avl_writedata;
	input avl_read;
	output [AVL_DATA_WIDTH - 1:0] avl_readdata;
	output avl_waitrequest;
	
	reg avl_waitrequest;
	reg [AVL_DATA_WIDTH - 1:0] avl_readdata_r;
	reg [AVL_DATA_WIDTH - 1:0] avl_readdata;

	
	typedef enum int unsigned {
		STATE_AVL_IDLE,
		STATE_AVL_DONE
	} STATE_AVL_T;

	STATE_AVL_T state_avl_curr;
	
	// State machine, AVALON side
		
	always_ff @ (posedge avl_clk or negedge avl_reset_n) begin
		if (avl_reset_n == 0) begin
			state_avl_curr <= STATE_AVL_IDLE;

			avl_readdata_r <= '0;

		end else begin
			case (state_avl_curr)
			STATE_AVL_IDLE: begin
				if (avl_read) begin
					// NIOS is reading parameters

					state_avl_curr <= STATE_AVL_DONE;
					
					case (avl_address[5:0])
					6'b000000: avl_readdata_r <= SIGNATURE;
					endcase
				end
			end
			STATE_AVL_DONE: begin
				// Done operation, wait until we are no longer selected by the
				// avalon bus.
				
				if (~avl_read && ~avl_write) begin
					state_avl_curr <= STATE_AVL_IDLE;
				end
			end
			endcase
		end
	end

	// wait request management and read data gating
	
	always_comb
	begin

		if (avl_read)
			avl_readdata <= avl_readdata_r;
		else 
			avl_readdata <= '0;
	end
	reg avl_read_r;
	always @(posedge avl_clk) begin
		avl_read_r <= avl_read;
	end
	assign avl_waitrequest = avl_read & ~avl_read_r;
	
endmodule
