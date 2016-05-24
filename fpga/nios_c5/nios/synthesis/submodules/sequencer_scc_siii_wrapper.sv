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

module sequencer_scc_siii_wrapper
    # (parameter
    
    DATAWIDTH               =   24,
    IO_SDATA_BITS           =   11,
    DQS_SDATA_BITS          =   46,
    AVL_DATA_WIDTH          =   32,
    DLL_DELAY_CHAIN_LENGTH  =   6
        
    )
    (
	
	reset_n_scc_clk,	
	scc_clk,
	scc_dataout,
	scc_io_cfg,
	scc_dqs_cfg
);

	input scc_clk;
	input reset_n_scc_clk;
	input [DATAWIDTH-1:0] scc_dataout;
	output    [IO_SDATA_BITS - 1:0] scc_io_cfg;
	output    [DQS_SDATA_BITS - 1:0] scc_dqs_cfg;
	
	typedef enum integer {
		SCC_ADDR_DQS_IN_DELAY	= 'b0001,
		SCC_ADDR_DQS_EN_PHASE	= 'b0010,
		SCC_ADDR_DQS_EN_DELAY	= 'b0011,
		SCC_ADDR_DQDQS_OUT_PHASE= 'b0100,
		SCC_ADDR_OCT_OUT1_DELAY	= 'b0101,
		SCC_ADDR_OCT_OUT2_DELAY	= 'b0110,
		SCC_ADDR_IO_OUT1_DELAY	= 'b0111,
		SCC_ADDR_IO_OUT2_DELAY	= 'b1000,
		SCC_ADDR_IO_IN_DELAY	= 'b1001
	} sdata_addr_t;
	
	wire    [DATAWIDTH-1:0] scc_dataout;
	reg     [IO_SDATA_BITS - 1:0] scc_io_cfg;
	reg     [DQS_SDATA_BITS - 1:0] scc_dqs_cfg;
	
	wire    [2:0] dqsi_phase;
	wire    [5:0] dqse_phase;
	wire    [6:0] dqs_phase;
	wire    [6:0] dq_phase;
	
	typedef bit [4:0] t_setting_mask;
	
	integer unsigned setting_offsets[1:9] = '{ 'd0, 'd4, 'd8, 'd12, 'd17, 'd21, 'd0, 'd4, 'd7 };
	t_setting_mask setting_masks [1:9] = '{ 'b01111, 'b01111, 'b01111, 'b11111, 'b01111, 'b00111, 'b01111, 'b00111, 'b01111 };
	
	// decode phases
	
	sequencer_scc_siii_phase_decode  # (
        .AVL_DATA_WIDTH         (AVL_DATA_WIDTH         ),
        .DLL_DELAY_CHAIN_LENGTH (DLL_DELAY_CHAIN_LENGTH )
    ) sequencer_scc_phase_decode_dqe_inst (
        .avl_writedata          ((scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_PHASE]) & setting_masks[SCC_ADDR_DQS_EN_PHASE]),
        .dqsi_phase	            (dqsi_phase	            ),
        .dqse_phase             (dqse_phase             )
    );
	
	sequencer_scc_siii_phase_decode  # (
        .AVL_DATA_WIDTH         (AVL_DATA_WIDTH         ),
        .DLL_DELAY_CHAIN_LENGTH (DLL_DELAY_CHAIN_LENGTH )
    ) sequencer_scc_phase_decode_dqdqs_inst (
        .avl_writedata          ((scc_dataout >> setting_offsets[SCC_ADDR_DQDQS_OUT_PHASE]) & setting_masks[SCC_ADDR_DQDQS_OUT_PHASE]),
        .dqs_phase              (dqs_phase              ),
        .dq_phase               (dq_phase               )
    );
	
	always_ff @ (posedge scc_clk or negedge reset_n_scc_clk) begin
		if (~reset_n_scc_clk) begin
			scc_io_cfg <= '0;
			scc_dqs_cfg <= '0;
		end
		else begin
			scc_dqs_cfg[23:19] <= '0;
			scc_dqs_cfg[25] <= '0;
			scc_dqs_cfg[37] <= '0;
			scc_dqs_cfg[42:41] <= '0;
			
			scc_dqs_cfg[6:4] <= dqsi_phase;
			
			scc_dqs_cfg[3:0] <= maskTo4Bits(scc_dataout >> setting_offsets[SCC_ADDR_DQS_IN_DELAY]);
			scc_dqs_cfg[29:27] <= maskTo3Bits(scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_DELAY]);
			scc_dqs_cfg[33:30] <= maskTo4Bits(scc_dataout >> setting_offsets[SCC_ADDR_OCT_OUT1_DELAY]);
			scc_dqs_cfg[36:34] <= maskTo3Bits(scc_dataout >> setting_offsets[SCC_ADDR_OCT_OUT2_DELAY]);
			scc_dqs_cfg[10:7] <= dqse_phase[5:2];
			scc_dqs_cfg[43] <= dqse_phase[1];
			scc_dqs_cfg[38] <= dqse_phase[0];
			scc_dqs_cfg[14:11] <= dqs_phase[6:3];
			scc_dqs_cfg[45] <= dqs_phase[2];
			scc_dqs_cfg[39] <= dqs_phase[1];
			scc_dqs_cfg[24] <= dqs_phase[0];                        
			scc_dqs_cfg[18:15] <= dq_phase[6:3];
			scc_dqs_cfg[44] <= dq_phase[2];
			scc_dqs_cfg[40] <= dq_phase[1];
			scc_dqs_cfg[26] <= dq_phase[0];
				
			scc_io_cfg[3:0] <= maskTo4Bits(scc_dataout >> setting_offsets[SCC_ADDR_IO_OUT1_DELAY]);
			scc_io_cfg[6:4] <= maskTo3Bits(scc_dataout >> setting_offsets[SCC_ADDR_IO_OUT2_DELAY]);
			scc_io_cfg[10:7] <= maskTo4Bits(scc_dataout >> setting_offsets[SCC_ADDR_IO_IN_DELAY]);
		end
	end

	function[2:0] maskTo3Bits;
		input[DATAWIDTH-1:0] i;
		maskTo3Bits = i[2:0];
	endfunction
	
	function[3:0] maskTo4Bits;
		input[DATAWIDTH-1:0] i;
		maskTo4Bits = i[3:0];
	endfunction

endmodule
