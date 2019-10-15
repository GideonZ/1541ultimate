//Legal Notice: (C)2019 Altera Corporation. All rights reserved.  Your
//use of Altera Corporation's design tools, logic functions and other
//software and tools, and its AMPP partner logic functions, and any
//output files any of the foregoing (including device programming or
//simulation files), and any associated documentation or information are
//expressly subject to the terms and conditions of the Altera Program
//License Subscription Agreement or other applicable license agreement,
//including, without limitation, that your use is for the sole purpose
//of programming logic devices manufactured by Altera and sold by Altera
//or its authorized distributors.  Please refer to the applicable
//agreement for further details.

// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 

module nios_solo_nios2_gen2_0_cpu_mult_cell (
                                              // inputs:
                                               E_src1,
                                               E_src2,
                                               M_en,
                                               clk,
                                               reset_n,

                                              // outputs:
                                               M_mul_cell_p1,
                                               M_mul_cell_p2,
                                               M_mul_cell_p3
                                            )
;

  output  [ 31: 0] M_mul_cell_p1;
  output  [ 31: 0] M_mul_cell_p2;
  output  [ 31: 0] M_mul_cell_p3;
  input   [ 31: 0] E_src1;
  input   [ 31: 0] E_src2;
  input            M_en;
  input            clk;
  input            reset_n;


wire    [ 31: 0] M_mul_cell_p1;
wire    [ 31: 0] M_mul_cell_p2;
wire    [ 31: 0] M_mul_cell_p3;
wire             mul_clr;
wire    [ 31: 0] mul_src1;
wire    [ 31: 0] mul_src2;
  assign mul_clr = ~reset_n;
  assign mul_src1 = E_src1;
  assign mul_src2 = E_src2;
  altera_mult_add the_altmult_add_p1
    (
      .aclr0 (mul_clr),
      .clock0 (clk),
      .dataa (mul_src1[15 : 0]),
      .datab (mul_src2[15 : 0]),
      .ena0 (M_en),
      .result (M_mul_cell_p1)
    );

  defparam the_altmult_add_p1.addnsub_multiplier_pipeline_aclr1 = "ACLR0",
           the_altmult_add_p1.addnsub_multiplier_pipeline_register1 = "CLOCK0",
           the_altmult_add_p1.addnsub_multiplier_register1 = "UNREGISTERED",
           the_altmult_add_p1.dedicated_multiplier_circuitry = "YES",
           the_altmult_add_p1.input_register_a0 = "UNREGISTERED",
           the_altmult_add_p1.input_register_b0 = "UNREGISTERED",
           the_altmult_add_p1.input_source_a0 = "DATAA",
           the_altmult_add_p1.input_source_b0 = "DATAB",
           the_altmult_add_p1.lpm_type = "altera_mult_add",
           the_altmult_add_p1.multiplier1_direction = "ADD",
           the_altmult_add_p1.multiplier_aclr0 = "ACLR0",
           the_altmult_add_p1.multiplier_register0 = "CLOCK0",
           the_altmult_add_p1.number_of_multipliers = 1,
           the_altmult_add_p1.output_register = "UNREGISTERED",
           the_altmult_add_p1.port_addnsub1 = "PORT_UNUSED",
           the_altmult_add_p1.port_addnsub3 = "PORT_UNUSED",
           the_altmult_add_p1.representation_a = "UNSIGNED",
           the_altmult_add_p1.representation_b = "UNSIGNED",
           the_altmult_add_p1.selected_device_family = "CYCLONEIVE",
           the_altmult_add_p1.signed_pipeline_aclr_a = "ACLR0",
           the_altmult_add_p1.signed_pipeline_aclr_b = "ACLR0",
           the_altmult_add_p1.signed_pipeline_register_a = "CLOCK0",
           the_altmult_add_p1.signed_pipeline_register_b = "CLOCK0",
           the_altmult_add_p1.signed_register_a = "UNREGISTERED",
           the_altmult_add_p1.signed_register_b = "UNREGISTERED",
           the_altmult_add_p1.width_a = 16,
           the_altmult_add_p1.width_b = 16,
           the_altmult_add_p1.width_result = 32;

  altera_mult_add the_altmult_add_p2
    (
      .aclr0 (mul_clr),
      .clock0 (clk),
      .dataa (mul_src1[15 : 0]),
      .datab (mul_src2[31 : 16]),
      .ena0 (M_en),
      .result (M_mul_cell_p2)
    );

  defparam the_altmult_add_p2.addnsub_multiplier_pipeline_aclr1 = "ACLR0",
           the_altmult_add_p2.addnsub_multiplier_pipeline_register1 = "CLOCK0",
           the_altmult_add_p2.addnsub_multiplier_register1 = "UNREGISTERED",
           the_altmult_add_p2.dedicated_multiplier_circuitry = "YES",
           the_altmult_add_p2.input_register_a0 = "UNREGISTERED",
           the_altmult_add_p2.input_register_b0 = "UNREGISTERED",
           the_altmult_add_p2.input_source_a0 = "DATAA",
           the_altmult_add_p2.input_source_b0 = "DATAB",
           the_altmult_add_p2.lpm_type = "altera_mult_add",
           the_altmult_add_p2.multiplier1_direction = "ADD",
           the_altmult_add_p2.multiplier_aclr0 = "ACLR0",
           the_altmult_add_p2.multiplier_register0 = "CLOCK0",
           the_altmult_add_p2.number_of_multipliers = 1,
           the_altmult_add_p2.output_register = "UNREGISTERED",
           the_altmult_add_p2.port_addnsub1 = "PORT_UNUSED",
           the_altmult_add_p2.port_addnsub3 = "PORT_UNUSED",
           the_altmult_add_p2.representation_a = "UNSIGNED",
           the_altmult_add_p2.representation_b = "UNSIGNED",
           the_altmult_add_p2.selected_device_family = "CYCLONEIVE",
           the_altmult_add_p2.signed_pipeline_aclr_a = "ACLR0",
           the_altmult_add_p2.signed_pipeline_aclr_b = "ACLR0",
           the_altmult_add_p2.signed_pipeline_register_a = "CLOCK0",
           the_altmult_add_p2.signed_pipeline_register_b = "CLOCK0",
           the_altmult_add_p2.signed_register_a = "UNREGISTERED",
           the_altmult_add_p2.signed_register_b = "UNREGISTERED",
           the_altmult_add_p2.width_a = 16,
           the_altmult_add_p2.width_b = 16,
           the_altmult_add_p2.width_result = 32;

  altera_mult_add the_altmult_add_p3
    (
      .aclr0 (mul_clr),
      .clock0 (clk),
      .dataa (mul_src1[31 : 16]),
      .datab (mul_src2[15 : 0]),
      .ena0 (M_en),
      .result (M_mul_cell_p3)
    );

  defparam the_altmult_add_p3.addnsub_multiplier_pipeline_aclr1 = "ACLR0",
           the_altmult_add_p3.addnsub_multiplier_pipeline_register1 = "CLOCK0",
           the_altmult_add_p3.addnsub_multiplier_register1 = "UNREGISTERED",
           the_altmult_add_p3.dedicated_multiplier_circuitry = "YES",
           the_altmult_add_p3.input_register_a0 = "UNREGISTERED",
           the_altmult_add_p3.input_register_b0 = "UNREGISTERED",
           the_altmult_add_p3.input_source_a0 = "DATAA",
           the_altmult_add_p3.input_source_b0 = "DATAB",
           the_altmult_add_p3.lpm_type = "altera_mult_add",
           the_altmult_add_p3.multiplier1_direction = "ADD",
           the_altmult_add_p3.multiplier_aclr0 = "ACLR0",
           the_altmult_add_p3.multiplier_register0 = "CLOCK0",
           the_altmult_add_p3.number_of_multipliers = 1,
           the_altmult_add_p3.output_register = "UNREGISTERED",
           the_altmult_add_p3.port_addnsub1 = "PORT_UNUSED",
           the_altmult_add_p3.port_addnsub3 = "PORT_UNUSED",
           the_altmult_add_p3.representation_a = "UNSIGNED",
           the_altmult_add_p3.representation_b = "UNSIGNED",
           the_altmult_add_p3.selected_device_family = "CYCLONEIVE",
           the_altmult_add_p3.signed_pipeline_aclr_a = "ACLR0",
           the_altmult_add_p3.signed_pipeline_aclr_b = "ACLR0",
           the_altmult_add_p3.signed_pipeline_register_a = "CLOCK0",
           the_altmult_add_p3.signed_pipeline_register_b = "CLOCK0",
           the_altmult_add_p3.signed_register_a = "UNREGISTERED",
           the_altmult_add_p3.signed_register_b = "UNREGISTERED",
           the_altmult_add_p3.width_a = 16,
           the_altmult_add_p3.width_b = 16,
           the_altmult_add_p3.width_result = 32;


endmodule

