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

// altera message_off 10230
module sequencer_scc_acv_wrapper
    # (parameter
    
    DATAWIDTH               =   24,
    IO_SDATA_BITS           =   11,
    DQS_SDATA_BITS          =   46,
    AVL_DATA_WIDTH          =   32,
    DLL_DELAY_CHAIN_LENGTH  =   6,
    USE_2X_DLL              =   "false"
        
    )
    (
	
	reset_n_scc_clk,	
	scc_clk,
	scc_dataout,
	scc_io_cfg,
	scc_dqs_cfg
);

	input  scc_clk;
	input  reset_n_scc_clk;
	input  [DATAWIDTH-1:0] scc_dataout;
	output [IO_SDATA_BITS - 1:0] scc_io_cfg;
	output [DQS_SDATA_BITS - 1:0] scc_dqs_cfg;
	
	typedef enum integer {
		SCC_ADDR_DQS_IN_DELAY	= 'b0001, 
		SCC_ADDR_DQS_EN_PHASE	= 'b0010, 
		SCC_ADDR_DQS_EN_DELAY	= 'b0011, 
		SCC_ADDR_DQDQS_OUT_PHASE= 'b0100, 
		SCC_ADDR_OCT_OUT1_DELAY	= 'b0101, 
		SCC_ADDR_OCT_OUT2_DELAY	= 'b0110, 
		SCC_ADDR_IO_OUT1_DELAY	= 'b0111, 
		SCC_ADDR_IO_OUT2_DELAY	= 'b1000, 
		SCC_ADDR_IO_IN_DELAY	= 'b1001, 
		SCC_ADDR_DQS_BYPASS		= 'b1010, 
		SCC_ADDR_DQ_BYPASS		= 'b1011, 
		SCC_ADDR_RFIFO_MODE		= 'b1100  
	} sdata_addr_t;
	
	wire    [DATAWIDTH-1:0] scc_dataout;
	reg     [IO_SDATA_BITS - 1:0] scc_io_cfg;
	reg     [DQS_SDATA_BITS - 1:0] scc_dqs_cfg;
	
	wire    [3:0] dqse_phase;
	
	typedef bit [6:0] t_setting_mask;

	integer unsigned setting_offsets[1:12] = '{ 'd0, 'd5, 'd8, 'd13, 'd13, 'd18, 'd0, 'd5, 'd5 , 'd18, 'd10, 'd11};
	t_setting_mask setting_masks [1:12] = '{ 'b011111, 'b0111, 'b011111, 'b0, 'b011111, 'b0, 'b011111, 'b0, 'b011111, 'b01, 'b01, 'b0111 };
	
	// decode phases
	
	sequencer_scc_acv_phase_decode  # (
		.AVL_DATA_WIDTH         (DATAWIDTH         ),
		.DLL_DELAY_CHAIN_LENGTH (DLL_DELAY_CHAIN_LENGTH ),
		.USE_2X_DLL             (USE_2X_DLL)
	) sequencer_scc_phase_decode_dqe_inst (
		.avl_writedata          ((scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_PHASE]) & setting_masks[SCC_ADDR_DQS_EN_PHASE]),
		.dqse_phase             (dqse_phase             )
	);

	always_ff @ (posedge scc_clk or negedge reset_n_scc_clk) begin
		if (~reset_n_scc_clk) begin
			scc_io_cfg <= '0;
			scc_dqs_cfg <= '0;
		end
		else begin
			scc_io_cfg <= '0;
			scc_dqs_cfg <= '0;


			// DQS
			
			// T11 Ungating (using same as T11 Gating for now)
			scc_dqs_cfg[4:0] <= (scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_DELAY]) & ({'0, setting_masks[SCC_ADDR_DQS_EN_DELAY]});
			
			// T11 Gating (using same as T11 ungating for now)
			scc_dqs_cfg[9:5] <= (scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_DELAY]) & ({'0, setting_masks[SCC_ADDR_DQS_EN_DELAY]});

			scc_dqs_cfg[10] <= dqse_phase[3]; 
			
			scc_dqs_cfg[15:11] <= (scc_dataout >> setting_offsets[SCC_ADDR_OCT_OUT1_DELAY]) & ({'0, setting_masks[SCC_ADDR_OCT_OUT1_DELAY]});
			
			// Bypass DQS Logic half-rate (0 engage, 1 bypass)
			scc_dqs_cfg[16] <= (scc_dataout >> setting_offsets[SCC_ADDR_DQS_BYPASS]) & ({'0, setting_masks[SCC_ADDR_DQS_BYPASS]});

			scc_dqs_cfg[21:17] <= (scc_dataout >> setting_offsets[SCC_ADDR_DQS_IN_DELAY]) & ({'0, setting_masks[SCC_ADDR_DQS_IN_DELAY]});

			scc_dqs_cfg[22] <= dqse_phase[2]; 
			
			scc_dqs_cfg[24:23] <= dqse_phase[1:0]; 


			// I/O
			
			scc_io_cfg[4:0] <= (scc_dataout >> setting_offsets[SCC_ADDR_IO_IN_DELAY]) & ({'0, setting_masks[SCC_ADDR_IO_IN_DELAY]});
			
			// T9 OE (using same as T9 output)
			scc_io_cfg[9:5] <= (scc_dataout >> setting_offsets[SCC_ADDR_IO_OUT1_DELAY]) & ({'0, setting_masks[SCC_ADDR_IO_OUT1_DELAY]});

			// T9 output (using same as T9 OE)
			scc_io_cfg[14:10] <= (scc_dataout >> setting_offsets[SCC_ADDR_IO_OUT1_DELAY]) & ({'0, setting_masks[SCC_ADDR_IO_OUT1_DELAY]});

			// Input register Read Fifo mode:
			//   000: half-rate read fifo
			//   001: full-rate read fifo
			//   010: deserializer bit slip
			//   011: deserializer with input from bit-slip
			//   100: deserializer with input from IO
			//   101: serializer mode
			scc_io_cfg[17:15] <= (scc_dataout >> setting_offsets[SCC_ADDR_RFIFO_MODE]) & ({'0, setting_masks[SCC_ADDR_RFIFO_MODE]});

			// Read FIFO Read Clock Source Select
			//   00: Select core CLKIN1
			//   01: Select DQS_CLK (PHY_CLK)
			//   10: Select SEQ_HR_CLK (PHY_CLK)
			//   11: Select VCC (Disabled)
			scc_io_cfg[19:18] <= (((scc_dataout >> setting_offsets[SCC_ADDR_RFIFO_MODE]) & ({'0, setting_masks[SCC_ADDR_RFIFO_MODE]})) == 3'b001) ? 2'b01 : 2'b10;

			// bypass IOE Register half-rate register
			//   0: engage half-rate register
			//   1: bypass half-rate register
			scc_io_cfg[20] <= (scc_dataout >> setting_offsets[SCC_ADDR_DQ_BYPASS]) & ({'0, setting_masks[SCC_ADDR_DQ_BYPASS]});
		end
	end
	
endmodule
