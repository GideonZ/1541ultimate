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

module nios_mem_if_ddr2_emif_0_p0_acv_ldc
(
	pll_hr_clk,
	pll_dq_clk,
	pll_dqs_clk,
	dll_phy_delayctrl,
	afi_clk,
	avl_clk,
	adc_clk,
	adc_clk_cps,
	hr_clk
);
parameter DLL_DELAY_CTRL_WIDTH  = "";
parameter ADC_PHASE_SETTING = 0;
parameter ADC_INVERT_PHASE = "false";
parameter IS_HHP_HPS = "false";

input pll_hr_clk;
input pll_dq_clk;
input pll_dqs_clk;
input [DLL_DELAY_CTRL_WIDTH-1:0]  dll_phy_delayctrl;
output afi_clk;
output avl_clk;
output adc_clk;
output adc_clk_cps;
output hr_clk;

wire phy_clk_dqs;
wire phy_clk_dq;
wire phy_clk_hr;
wire phy_clk_dqs_2x;
wire phy_clk_addr_cmd;
wire phy_clk_addr_cmd_cps;


generate
if (IS_HHP_HPS == "true") begin
	assign phy_clk_hr = pll_hr_clk;
	assign phy_clk_dq = pll_dq_clk;
	assign phy_clk_dqs = pll_dqs_clk;
	assign phy_clk_dqs_2x = 1'b0;
end else begin
	cyclonev_phy_clkbuf phy_clkbuf (
	        .inclk ({pll_hr_clk, pll_dq_clk, pll_dqs_clk, 1'b0}),
        	.outclk ({phy_clk_hr, phy_clk_dq, phy_clk_dqs, phy_clk_dqs_2x})
	);
end
endgenerate
			      
wire [3:0] leveled_dqs_clocks;
wire [3:0] leveled_hr_clocks;
wire hr_seq_clock;

cyclonev_leveling_delay_chain leveling_delay_chain_dqs (
        .clkin (phy_clk_dqs),
        .delayctrlin (dll_phy_delayctrl),
        .clkout(leveled_dqs_clocks)
);
defparam leveling_delay_chain_dqs.physical_clock_source = "DQS";
assign afi_clk = leveled_dqs_clocks[0];

cyclonev_leveling_delay_chain leveling_delay_chain_hr (
    .clkin (phy_clk_hr),
    .delayctrlin (),
    .clkout(leveled_hr_clocks)
);
defparam leveling_delay_chain_hr.physical_clock_source = "HR";
assign avl_clk = leveled_hr_clocks[0];

cyclonev_clk_phase_select clk_phase_select_addr_cmd (
	.clkin(leveled_dqs_clocks),
	.clkout(adc_clk_cps)
);
defparam clk_phase_select_addr_cmd.physical_clock_source = "ADD_CMD";
defparam clk_phase_select_addr_cmd.use_phasectrlin = "false";
defparam clk_phase_select_addr_cmd.phase_setting = ADC_PHASE_SETTING;
defparam clk_phase_select_addr_cmd.invert_phase = ADC_INVERT_PHASE;

cyclonev_clk_phase_select clk_phase_select_hr (
    .phasectrlin(),
    .phaseinvertctrl(),
    .dqsin(),
`ifndef SIMGEN
    .clkin (leveled_hr_clocks[0]),
`else
    .clkin (leveled_hr_clocks),
`endif
    .clkout (hr_seq_clock)
);
defparam clk_phase_select_hr.physical_clock_source = "HR";
defparam clk_phase_select_hr.use_phasectrlin = "false";
defparam clk_phase_select_hr.phase_setting = 0;
assign hr_clk = hr_seq_clock;

generate
if (ADC_INVERT_PHASE == "true")
begin
	assign adc_clk = ~leveled_dqs_clocks[ADC_PHASE_SETTING];
end else begin
	assign adc_clk = leveled_dqs_clocks[ADC_PHASE_SETTING];
end
endgenerate

endmodule
