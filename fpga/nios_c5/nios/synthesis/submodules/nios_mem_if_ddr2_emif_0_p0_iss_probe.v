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

module nios_mem_if_ddr2_emif_0_p0_iss_probe (
	probe_input
);
parameter WIDTH = 1;
parameter ID_NAME = "PROB";

input [WIDTH-1:0] probe_input;

	
altsource_probe	iss_probe_inst (
				.probe (probe_input),
				.source ()
				// synopsys translate_off
				,
				.clr (),
				.ena (),
				.ir_in (),
				.ir_out (),
				.jtag_state_cdr (),
				.jtag_state_cir (),
				.jtag_state_e1dr (),
				.jtag_state_sdr (),
				.jtag_state_tlr (),
				.jtag_state_udr (),
				.jtag_state_uir (),
				.raw_tck (),
				.source_clk (),
				.source_ena (),
				.tdi (),
				.tdo (),
				.usr1 ()
				// synopsys translate_on
				);
	defparam
		iss_probe_inst.enable_metastability = "NO",
		iss_probe_inst.instance_id = ID_NAME,
		iss_probe_inst.probe_width = WIDTH,
		iss_probe_inst.sld_auto_instance_index = "YES",
		iss_probe_inst.sld_instance_index = 0,
		iss_probe_inst.source_initial_value = "0",
		iss_probe_inst.source_width = 0;
		
		
endmodule
