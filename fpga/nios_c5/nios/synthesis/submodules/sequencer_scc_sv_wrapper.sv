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

module sequencer_scc_sv_wrapper
    # (parameter
    
    DATAWIDTH               =   24,
    IO_SDATA_BITS           =   11,
    DQS_SDATA_BITS          =   46,
    AVL_DATA_WIDTH          =   32,
    DLL_DELAY_CHAIN_LENGTH  =   6,
    DUAL_WRITE_CLOCK	    =   0
        
    )
    (
	
	reset_n_scc_clk,	
	scc_clk,
	scc_dataout,
	scc_io_cfg,
	scc_dqs_cfg,
	scc_sr_dqsenable_delayctrl,
	scc_sr_dqsdisablen_delayctrl,
	scc_sr_multirank_delayctrl
);

	input scc_clk;
	input reset_n_scc_clk;
	input [DATAWIDTH-1:0] scc_dataout;
	output    [IO_SDATA_BITS - 1:0] scc_io_cfg;
	output    [DQS_SDATA_BITS - 1:0] scc_dqs_cfg;
	
	output [7:0] scc_sr_dqsenable_delayctrl;
	output [7:0] scc_sr_dqsdisablen_delayctrl;
	output [7:0] scc_sr_multirank_delayctrl;
	
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
	reg     [7:0] scc_sr_dqsenable_delayctrl;
	reg     [7:0] scc_sr_dqsdisablen_delayctrl;
	reg     [7:0] scc_sr_multirank_delayctrl;
	
	wire    [2:0] dqsi_phase;
	wire    [5:0] dqse_phase;
	wire    [6:0] dqs_phase;
	wire    [6:0] dq_phase;
	
	typedef bit [13:0] t_setting_mask;
	
	integer unsigned setting_offsets[1:9] = '{ 'd0, 'd12, 'd17, 'd25, 'd30, 'd36, 'd0, 'd6, 'd12 };
	t_setting_mask setting_masks [1:9] = '{ 'b0111111111111, 'b011111, 'b011111111, 'b011111, 'b0111111, 'b0111111, 'b0111111, 'b0111111, 'b0111111111111 };
	
	// decode phases
	
	sequencer_scc_sv_phase_decode  # (
        .AVL_DATA_WIDTH         (DATAWIDTH         ),
        .DLL_DELAY_CHAIN_LENGTH (DLL_DELAY_CHAIN_LENGTH )
    ) sequencer_scc_phase_decode_dqe_inst (
        .avl_writedata          ((scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_PHASE]) & setting_masks[SCC_ADDR_DQS_EN_PHASE]),
        .dqsi_phase	            (dqsi_phase	            ),
        .dqse_phase             (dqse_phase             ),
				.dqs_phase(),
				.dq_phase()
    );
	
	sequencer_scc_sv_phase_decode  # (
        .AVL_DATA_WIDTH         (DATAWIDTH         ),
        .DLL_DELAY_CHAIN_LENGTH (DLL_DELAY_CHAIN_LENGTH )
    ) sequencer_scc_phase_decode_dqdqs_inst (
        .avl_writedata          ((scc_dataout >> setting_offsets[SCC_ADDR_DQDQS_OUT_PHASE]) & setting_masks[SCC_ADDR_DQDQS_OUT_PHASE]),
        .dqs_phase              (dqs_phase              ),
        .dq_phase               (dq_phase               ),
				.dqsi_phase(),
				.dqse_phase()
    );
	
	always_ff @ (posedge scc_clk or negedge reset_n_scc_clk) begin
		if (~reset_n_scc_clk) begin
			scc_io_cfg <= '0;
			scc_dqs_cfg <= '0;

			scc_sr_dqsenable_delayctrl <= '0;
			scc_sr_dqsdisablen_delayctrl <= '0;
			scc_sr_multirank_delayctrl <= '0;
			
			scc_dqs_cfg[26] <= '1;
			scc_dqs_cfg[30] <= '1;  
			scc_dqs_cfg[35] <= '1; 
			scc_dqs_cfg[39] <= '1; 
			scc_dqs_cfg[44] <= '1; 
			scc_dqs_cfg[48] <= '1;
			scc_dqs_cfg[53] <= '1;
			scc_dqs_cfg[57] <= '1; 
			scc_dqs_cfg[63] <= '1; 

		end
		else begin
			scc_dqs_cfg <= '0;
		
			scc_dqs_cfg[26] <= '1;
			scc_dqs_cfg[30] <= '1;  
			scc_dqs_cfg[35] <= '1; 
			scc_dqs_cfg[39] <= '1; 
			scc_dqs_cfg[44] <= '1; 
			scc_dqs_cfg[48] <= '1;
			scc_dqs_cfg[53] <= '1;
			scc_dqs_cfg[57] <= '1; 
			scc_dqs_cfg[63] <= '1; 
		
	
			scc_dqs_cfg[88:87] <= dqsi_phase[1:0]; 
			
			scc_dqs_cfg[11:0] <= maskTo12Bits(scc_dataout >> setting_offsets[SCC_ADDR_DQS_IN_DELAY]); 
			scc_dqs_cfg[85:78] <= maskTo8Bits(scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_DELAY]); 
			scc_dqs_cfg[77:70] <= maskTo8Bits(scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_DELAY]); 
			scc_dqs_cfg[17:12] <= maskTo6Bits(scc_dataout >> setting_offsets[SCC_ADDR_OCT_OUT1_DELAY]); 
			scc_dqs_cfg[23:18] <= maskTo6Bits(scc_dataout >> setting_offsets[SCC_ADDR_OCT_OUT2_DELAY]); 
		

			scc_dqs_cfg[42:41] <= dqse_phase[1:0]; 

			scc_dqs_cfg[45] <= dqse_phase[2]; 
			scc_dqs_cfg[86] <= dqse_phase[3]; 
			if (DUAL_WRITE_CLOCK) begin
				scc_dqs_cfg[29:28] <= dqs_phase[6:5]; 
				scc_dqs_cfg[31] <= dqs_phase[4]; 
				scc_dqs_cfg[69] <= dqs_phase[0]; 
				scc_dqs_cfg[89] <= dqs_phase[0]; 

				scc_dqs_cfg[68:66] <= dqs_phase[3:1];   
				scc_dqs_cfg[93:91] <= dqs_phase[3:1];   

				scc_dqs_cfg[47:46] <= dqs_phase[6:5]; 
				
				scc_dqs_cfg[33:32] <= dqs_phase[6:5]; 
				scc_dqs_cfg[36] <= dqs_phase[4]; 
				scc_dqs_cfg[90] <= dqs_phase[0]; 
				scc_dqs_cfg[96:94] <= dqs_phase[3:1]; 

				scc_dqs_cfg[51:50] <= dqs_phase[6:5]; 

				scc_dqs_cfg[34] <= '1;
				scc_dqs_cfg[52] <= '1;
			end
			else begin
				scc_dqs_cfg[29:28] <= dqs_phase[6:5]; 
				scc_dqs_cfg[31] <= dqs_phase[4]; 
				scc_dqs_cfg[69] <= dqs_phase[0]; 
				scc_dqs_cfg[89] <= dqs_phase[0]; 

				scc_dqs_cfg[68:66] <= dqs_phase[3:1];   
				scc_dqs_cfg[93:91] <= dqs_phase[3:1];   

				scc_dqs_cfg[47:46] <= dqs_phase[6:5]; 
				
				scc_dqs_cfg[33:32] <= dq_phase[6:5]; 
				scc_dqs_cfg[36] <= dq_phase[4]; 
				scc_dqs_cfg[90] <= dq_phase[0]; 
				scc_dqs_cfg[96:94] <= dq_phase[3:1]; 

				scc_dqs_cfg[51:50] <= dq_phase[6:5]; 
			end

			scc_dqs_cfg[54] <= 1'b0; 
			scc_dqs_cfg[49] <= 1'b0;

			scc_io_cfg[17:12] <= maskTo6Bits(scc_dataout >> setting_offsets[SCC_ADDR_IO_OUT1_DELAY]); 
			scc_io_cfg[23:18] <= maskTo6Bits(scc_dataout >> setting_offsets[SCC_ADDR_IO_OUT2_DELAY]); 
			scc_io_cfg[11:0] <= maskTo12Bits(scc_dataout >> setting_offsets[SCC_ADDR_IO_IN_DELAY]); 
			scc_io_cfg[39:24] <= '0; 
			
			scc_sr_dqsenable_delayctrl <= maskTo8Bits(scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_DELAY]); 
			scc_sr_dqsdisablen_delayctrl <= maskTo8Bits(scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_DELAY]); 
			scc_sr_multirank_delayctrl <= maskTo8Bits(scc_dataout >> setting_offsets[SCC_ADDR_DQS_EN_DELAY]); 
		end
	end

	function[5:0] maskTo6Bits;
		input[DATAWIDTH-1:0] i;
		maskTo6Bits = i[5:0];
	endfunction
	
	function[7:0] maskTo8Bits;
		input[DATAWIDTH-1:0] i;
		maskTo8Bits = i[7:0];
	endfunction

	function[11:0] maskTo12Bits;
		input[DATAWIDTH-1:0] i;
		maskTo12Bits = i[11:0];
	endfunction

endmodule
