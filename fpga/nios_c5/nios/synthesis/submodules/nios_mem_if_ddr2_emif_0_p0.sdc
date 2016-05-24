# (C) 2001-2015 Altera Corporation. All rights reserved.
# Your use of Altera Corporation's design tools, logic functions and other 
# software and tools, and its AMPP partner logic functions, and any output 
# files any of the foregoing (including device programming or simulation 
# files), and any associated documentation or information are expressly subject 
# to the terms and conditions of the Altera Program License Subscription 
# Agreement, Altera MegaCore Function License Agreement, or other applicable 
# license agreement, including, without limitation, that your use is for the 
# sole purpose of programming logic devices manufactured by Altera and sold by 
# Altera or its authorized distributors.  Please refer to the applicable 
# agreement for further details.


#####################################################################
#
# THIS IS AN AUTO-GENERATED FILE!
# -------------------------------
# If you modify this files, all your changes will be lost if you
# regenerate the core!
#
# FILE DESCRIPTION
# ----------------
# This file contains the timing constraints for the UniPHY memory
# interface.
#    * The timing parameters used by this file are assigned
#      in the nios_mem_if_ddr2_emif_0_p0_timing.tcl script.
#    * The helper routines are defined in nios_mem_if_ddr2_emif_0_p0_pin_map.tcl
#
# NOTE
# ----

set script_dir [file dirname [info script]]

source "$script_dir/nios_mem_if_ddr2_emif_0_p0_parameters.tcl"
source "$script_dir/nios_mem_if_ddr2_emif_0_p0_timing.tcl"
source "$script_dir/nios_mem_if_ddr2_emif_0_p0_pin_map.tcl"

load_package ddr_timing_model

set synthesis_flow 0
set sta_flow 0
set fit_flow 0
if { $::TimeQuestInfo(nameofexecutable) == "quartus_map" } {
	set synthesis_flow 1
} elseif { $::TimeQuestInfo(nameofexecutable) == "quartus_sta" } {
	set sta_flow 1
} elseif { $::TimeQuestInfo(nameofexecutable) == "quartus_fit" } {
	set fit_flow 1
}

####################
#                  #
# GENERAL SETTINGS #
#                  #
####################

# This is a global setting and will apply to the whole design.
# This setting is required for the memory interface to be
# properly constrained.
derive_clock_uncertainty

# Debug switch. Change to 1 to get more run-time debug information
set debug 0

# All timing requirements will be represented in nanoseconds with up to 3 decimal places of precision
set_time_format -unit ns -decimal_places 3

# Determine if entity names are on
set entity_names_on [ nios_mem_if_ddr2_emif_0_p0_are_entity_names_on ]

##################
#                #
# QUERIED TIMING #
#                #
##################

set io_standard "$::GLOBAL_nios_mem_if_ddr2_emif_0_p0_io_standard CLASS I"

# This is the peak-to-peak jitter on the whole read capture path
set DQSpathjitter [expr [get_micro_node_delay -micro DQDQS_JITTER -parameters [list IO] -in_fitter]/1000.0]

# This is the proportion of the DQ-DQS read capture path jitter that applies to setup
set DQSpathjitter_setup_prop [expr [get_micro_node_delay -micro DQDQS_JITTER_DIVISION -parameters [list IO] -in_fitter]/100.0]

# This is the peak-to-peak jitter, of which half is considered to be tJITper
set tJITper [expr [get_micro_node_delay -micro MEM_CK_PERIOD_JITTER -parameters [list IO PHY_SHORT] -in_fitter -period $t(CK)]/2000.0 + $SSN(pullin_o)]

##################
#                #
# DERIVED TIMING #
#                #
##################

# These parameters are used to make constraints more readeable

# Half of memory clock cycle
set half_period [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr $t(CK) / 2.0 ] ]

# Half of reference clock
set ref_half_period [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr $t(refCK) / 2.0 ] ]

# Minimum delay on data output pins
set t(wru_output_min_delay_external) [expr $t(DH) + $board(intra_DQS_group_skew) + $ISI(DQ)/2 + $ISI(DQS)/2 - $board(DQ_DQS_skew)]
set t(wru_output_min_delay_internal) [expr $t(WL_DCD) + $t(WL_JITTER)*(1.0-$t(WL_JITTER_DIVISION)) + $SSN(rel_pullin_o)]
set data_output_min_delay [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr - $t(wru_output_min_delay_external) - $t(wru_output_min_delay_internal)]]

# Maximum delay on data output pins
set t(wru_output_max_delay_external) [expr $t(DS) + $board(intra_DQS_group_skew) + $ISI(DQ)/2 + $ISI(DQS)/2 + $board(DQ_DQS_skew)]
set t(wru_output_max_delay_internal) [expr $t(WL_DCD) + $t(WL_JITTER)*$t(WL_JITTER_DIVISION) + $SSN(rel_pushout_o)]
set data_output_max_delay [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr $t(wru_output_max_delay_external) + $t(wru_output_max_delay_internal)]]

# Maximum delay on data input pins
set t(rdu_input_max_delay_external) [expr $t(DQSQ) + $board(intra_DQS_group_skew) + $board(DQ_DQS_skew) + $ISI(READ_DQ)/2 + $ISI(READ_DQS)/2]
set t(rdu_input_max_delay_internal) [expr $DQSpathjitter*$DQSpathjitter_setup_prop + $SSN(rel_pushout_i)]
set data_input_max_delay [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr $t(rdu_input_max_delay_external) + $t(rdu_input_max_delay_internal) ]]

# Minimum delay on data input pins
set t(rdu_input_min_delay_external) [expr $board(intra_DQS_group_skew) - $board(DQ_DQS_skew) + $ISI(READ_DQ)/2 + $ISI(READ_DQS)/2]
set t(rdu_input_min_delay_internal) [expr $t(DCD) + $DQSpathjitter*(1.0-$DQSpathjitter_setup_prop) + $SSN(rel_pullin_i)]
set data_input_min_delay [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr - $t(rdu_input_min_delay_external) - $t(rdu_input_min_delay_internal) ]]

# Minimum delay on address and command paths
set ac_min_delay [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr - $t(IH) -$fpga(tPLL_JITTER) - $fpga(tPLL_PSERR) - $board(intra_addr_ctrl_skew) + $board(addresscmd_CK_skew) - $ISI(addresscmd_hold) ]]

# Maximum delay on address and command paths
set ac_max_delay [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr $t(IS) +$fpga(tPLL_JITTER) + $fpga(tPLL_PSERR) + $board(intra_addr_ctrl_skew) + $board(addresscmd_CK_skew) + $ISI(addresscmd_setup) ]]

if { $debug } {
	post_message -type info "SDC: Computed Parameters:"
	post_message -type info "SDC: --------------------"
	post_message -type info "SDC: half_period: $half_period"
	post_message -type info "SDC: data_output_min_delay: $data_output_min_delay"
	post_message -type info "SDC: data_output_max_delay: $data_output_max_delay"
	post_message -type info "SDC: data_input_min_delay: $data_input_min_delay"
	post_message -type info "SDC: data_input_max_delay: $data_input_max_delay"
	post_message -type info "SDC: ac_min_delay: $ac_min_delay"
	post_message -type info "SDC: ac_max_delay: $ac_max_delay"
	post_message -type info "SDC: Using Timing Models: Micro"
}

# This is the main call to the netlist traversal routines
# that will automatically find all pins and registers required
# to apply timing constraints.
# During the fitter, the routines will be called only once
# and cached data will be used in all subsequent calls.
if { ! [ info exists nios_mem_if_ddr2_emif_0_p0_sdc_cache ] } {
	set nios_mem_if_ddr2_emif_0_p0_sdc_cache 1
	nios_mem_if_ddr2_emif_0_p0_initialize_ddr_db nios_mem_if_ddr2_emif_0_p0_ddr_db
} else {
	if { $debug } {
		post_message -type info "SDC: reusing cached DDR DB"
	}
}

# If multiple instances of this core are present in the
# design they will all be constrained through the
# following loop
set instances [ array names nios_mem_if_ddr2_emif_0_p0_ddr_db ]
foreach { inst } $instances {
	if { [ info exists pins ] } {
		# Clean-up stale content
		unset pins
	}
	array set pins $nios_mem_if_ddr2_emif_0_p0_ddr_db($inst)

	set prefix $inst
	if { $entity_names_on } {
		set prefix [ string map "| |*:" $inst ]
		set prefix "*:$prefix"
	}

	#####################################################
	#                                                   #
	# Transfer the pin names to more readable variables #
	#                                                   #
	#####################################################

	set dqs_pins $pins(dqs_pins)
	set dqsn_pins $pins(dqsn_pins)
	set q_groups [ list ]
	foreach { q_group } $pins(q_groups) {
		set q_group $q_group
		lappend q_groups $q_group
	}
	set all_dq_pins [ join [ join $q_groups ] ]

	set ck_pins $pins(ck_pins)
	set ckn_pins $pins(ckn_pins)
	set add_pins $pins(add_pins)
	set ba_pins $pins(ba_pins)
	set cmd_pins $pins(cmd_pins)
	set ac_pins [ concat $add_pins $ba_pins $cmd_pins ]
	set dm_pins $pins(dm_pins)
	set all_dq_dm_pins [ concat $all_dq_pins $dm_pins ]

	set pll_ref_clock $pins(pll_ref_clock)
	set pll_afi_clock $pins(pll_afi_clock)
	set pll_afi_phy_clock $pins(pll_afi_phy_clock)
	set pll_ac_clock $pins(pll_ac_clock)
	set pll_dq_write_clock $pins(pll_dq_write_clock)
	set pll_ck_clock $pins(pll_ck_clock)
	set pll_write_clock $pins(pll_write_clock)
	set pll_avl_clock $pins(pll_avl_clock)
	set pll_config_clock $pins(pll_config_clock)

	set dqs_in_clocks $pins(dqs_in_clocks)
	set dqs_out_clocks $pins(dqs_out_clocks)
	set dqsn_out_clocks $pins(dqsn_out_clocks)

	set afi_reset_reg $pins(afi_reset_reg)
	set seq_reset_reg $pins(seq_reset_reg)
	set sync_reg $pins(sync_reg)
	set read_capture_ddio $pins(read_capture_ddio)
	set fifo_wraddress_reg $pins(fifo_wraddress_reg)
	set fifo_rdaddress_reg $pins(fifo_rdaddress_reg)
	set fifo_wrdata_reg $pins(fifo_wrdata_reg)
	set fifo_rddata_reg $pins(fifo_rddata_reg)

	##################
	#                #
	# QUERIED TIMING #
	#                #
	##################

	# Phase Jitter on DQS paths. This parameter is queried at run time
	set fpga(tDQS_PHASE_JITTER) [ expr [ get_integer_node_delay -integer $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_dqs_delay_chain_length -parameters {IO MAX HIGH} -src DQS_PHASE_JITTER -in_fitter ] / 1000.0 ]

	# Phase Error on DQS paths. This parameter is queried at run time
	set fpga(tDQS_PSERR) [ expr [ get_integer_node_delay -integer $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_dqs_delay_chain_length -parameters {IO MAX HIGH} -src DQS_PSERR -in_fitter ] / 1000.0 ]

	# Correct input min/max delay for queried parameters
	set t(rdu_input_min_delay_external) [expr $t(rdu_input_min_delay_external) + ($t(CK)/2.0 - $t(QH_time))]
	set t(rdu_input_min_delay_internal) [expr $t(rdu_input_min_delay_internal) + $fpga(tDQS_PSERR) + $tJITper]
	set t(rdu_input_max_delay_external) [expr $t(rdu_input_max_delay_external)]
	set t(rdu_input_max_delay_internal) [expr $t(rdu_input_max_delay_internal) + $fpga(tDQS_PSERR)]

	set final_data_input_max_delay [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr $data_input_max_delay + $fpga(tDQS_PSERR) ]]
	set final_data_input_min_delay [ nios_mem_if_ddr2_emif_0_p0_round_3dp [ expr $data_input_min_delay - $t(CK) / 2.0 + $t(QH_time) - $fpga(tDQS_PSERR) - $tJITper]]

	if { $debug } {
		post_message -type info "SDC: Jitter Parameters"
		post_message -type info "SDC: -----------------"
		post_message -type info "SDC:    DQS Phase: $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_dqs_delay_chain_length"
		post_message -type info "SDC:    fpga(tDQS_PHASE_JITTER): $fpga(tDQS_PHASE_JITTER)"
		post_message -type info "SDC:    fpga(tDQS_PSERR): $fpga(tDQS_PSERR)"
		post_message -type info "SDC:    t(QH_time): $t(QH_time)"
		post_message -type info "SDC:"
		post_message -type info "SDC: Derived Parameters:"
		post_message -type info "SDC: -----------------"
		post_message -type info "SDC:    Corrected data_input_max_delay: $final_data_input_max_delay"
		post_message -type info "SDC:    Corrected data_input_min_delay: $final_data_input_min_delay"
		post_message -type info "SDC: -----------------"
	}

	# ----------------------- #
	# -                     - #
	# --- REFERENCE CLOCK --- #
	# -                     - #
	# ----------------------- #

	# This is the reference clock used by the PLL to derive any other clock in the core
	if { [get_collection_size [get_clocks -nowarn $pll_ref_clock]] > 0 } { remove_clock $pll_ref_clock }
	create_clock -period $t(refCK) -waveform [ list 0 $ref_half_period ] $pll_ref_clock

	# ------------------ #
	# -                - #
	# --- PLL CLOCKS --- #
	# -                - #
	# ------------------ #
	
	# AFI clock
	set local_pll_afi_clk [ nios_mem_if_ddr2_emif_0_p0_get_or_add_clock_vseries \
		-target $pll_afi_clock \
		-suffix "afi_clk" \
		-source $pll_ref_clock \
		-multiply_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_AFI_CLK) \
		-divide_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_AFI_CLK) \
		-phase $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(PLL_AFI_CLK) ]
	
	# HR PHY clock
	set local_pll_afi_phy_clk [ nios_mem_if_ddr2_emif_0_p0_get_or_add_clock_vseries \
		-target $pll_afi_phy_clock \
		-suffix "afi_phy_clk" \
		-source $pll_ref_clock \
		-multiply_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_AFI_CLK) \
		-divide_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_AFI_CLK) \
		-phase $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(PLL_AFI_CLK) ]
	# DQ write clock
	set local_pll_dq_write_clk [ nios_mem_if_ddr2_emif_0_p0_get_or_add_clock_vseries \
		-target $pll_dq_write_clock \
		-suffix "dq_write_clk" \
		-source $pll_ref_clock \
		-multiply_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_WRITE_CLK) \
		-divide_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_WRITE_CLK) \
		-phase $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(PLL_WRITE_CLK) ]
	
	# DQS write clock
	set local_pll_write_clk [ nios_mem_if_ddr2_emif_0_p0_get_or_add_clock_vseries \
		-target $pll_write_clock \
		-suffix "write_clk" \
		-source $pll_ref_clock \
		-multiply_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_MEM_CLK) \
		-divide_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_MEM_CLK) \
		-phase $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(PLL_MEM_CLK) ]

	# A/C clock
	set local_pll_addr_cmd_clk [ nios_mem_if_ddr2_emif_0_p0_get_or_add_clock_vseries \
		-target $pll_ac_clock \
		-suffix "addr_cmd_clk" \
		-source $pll_ref_clock \
		-multiply_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_ADDR_CMD_CLK) \
		-divide_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_ADDR_CMD_CLK) \
		-phase $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(PLL_ADDR_CMD_CLK) ]

	# NIOS clock
	set local_pll_avl_clock [ nios_mem_if_ddr2_emif_0_p0_get_or_add_clock_vseries \
		-target $pll_avl_clock \
		-suffix "avl_clk" \
		-source $pll_ref_clock \
		-multiply_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_NIOS_CLK) \
		-divide_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_NIOS_CLK) \
		-phase $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(PLL_NIOS_CLK) ]

	set mem_factor [expr double($::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_MEM_CLK)) / $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_MEM_CLK)]
	set avl_factor [expr double($::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_NIOS_CLK)) / $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_NIOS_CLK)]
	set write_to_avl_clk_ratio [expr int($mem_factor / $avl_factor)]
	

	# I/O scan chain clock
	set local_pll_config_clock [ nios_mem_if_ddr2_emif_0_p0_get_or_add_clock_vseries \
		-target $pll_config_clock \
		-suffix "config_clk" \
		-source $pll_ref_clock \
		-multiply_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_mult(PLL_CONFIG_CLK) \
		-divide_by $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_div(PLL_CONFIG_CLK) \
		-phase $::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(PLL_CONFIG_CLK) ]	





	# -------------------- #
	# -                  - #
	# --- SYSTEM CLOCK --- #
	# -                  - #
	# -------------------- #

	# This is the CK clock
	foreach { ck_pin } $ck_pins {
		create_generated_clock -multiply_by 1 -source $pll_write_clock -master_clock "$local_pll_write_clk" $ck_pin -name $ck_pin
		set_clock_uncertainty -to [ get_clocks $ck_pin ] $t(WL_JITTER)
	}

	# This is the CK#clock
	foreach { ckn_pin } $ckn_pins {
		create_generated_clock -multiply_by 1 -invert -source $pll_write_clock -master_clock "$local_pll_write_clk" $ckn_pin -name $ckn_pin
		set_clock_uncertainty -to [ get_clocks $ckn_pin ] $t(WL_JITTER)
	}
	
	# ------------------- #
	# -                 - #
	# --- READ CLOCKS --- #
	# -                 - #
	# ------------------- #

	foreach dqs_in_clock_struct $dqs_in_clocks {
		array set dqs_in_clock $dqs_in_clock_struct
		# This is the DQS clock for Read Capture analysis (micro model)
		create_clock -period $t(CK) -waveform [ list 0 $half_period ] $dqs_in_clock(dqs_pin) -name $dqs_in_clock(dqs_pin)_IN -add

		# Clock Uncertainty is accounted for by the ...pathjitter parameters
		set_clock_uncertainty -from [ get_clocks $dqs_in_clock(dqs_pin)_IN ] 0
	}

	# -------------------- #
	# -                  - #
	# --- WRITE CLOCKS --- #
	# -                  - #
	# -------------------- #

	# This is the DQS clock for Data Write analysis (micro model)
	foreach dqs_out_clock_struct $dqs_out_clocks {
		array set dqs_out_clock $dqs_out_clock_struct
		create_generated_clock -multiply_by 1 -master_clock [get_clocks $local_pll_write_clk] -source $pll_write_clock $dqs_out_clock(dst) -name $dqs_out_clock(dst)_OUT -add

		# Clock Uncertainty is accounted for by the ...pathjitter parameters
		set_clock_uncertainty -to [ get_clocks $dqs_out_clock(dst)_OUT ] 0
	}

	# This is the DQS#clock for Data Write analysis (micro model)
	foreach dqsn_out_clock_struct $dqsn_out_clocks {
		array set dqsn_out_clock $dqsn_out_clock_struct
		create_generated_clock -multiply_by 1 -master_clock [get_clocks $local_pll_write_clk] -source $pll_write_clock $dqsn_out_clock(dst) -name $dqsn_out_clock(dst)_OUT -add

		# Clock Uncertainty is accounted for by the ...pathjitter parameters
		set_clock_uncertainty -to [ get_clocks $dqsn_out_clock(dst)_OUT ] 0
	}

	##################
	#                #
	# READ DATA PATH #
	#                #
	##################

	foreach { dqs_pin } $dqs_pins { dq_pins } $q_groups {
		foreach { dq_pin } $dq_pins {
			if {[get_collection_size [get_registers -nowarn $read_capture_ddio]] > 0} {
				set_max_delay -from [get_ports $dq_pin] -to $read_capture_ddio 0
				set_min_delay -from [get_ports $dq_pin] -to $read_capture_ddio [expr 0-$half_period]
			}

			# Specifies the maximum delay difference between the DQ pin and the DQS pin:
			set_input_delay -max $final_data_input_max_delay -clock [get_clocks ${dqs_pin}_IN ] [get_ports $dq_pin] -add_delay

			# Specifies the minimum delay difference between the DQ pin and the DQS pin:
			set_input_delay -min $final_data_input_min_delay -clock [get_clocks ${dqs_pin}_IN ] [get_ports $dq_pin] -add_delay
		}
	}

	###################
	#                 #
	# WRITE DATA PATH #
	#                 #
	###################

	foreach { dqs_pin } $dqs_pins { dq_pins } $q_groups {
		foreach { dq_pin } $dq_pins {
			# Specifies the minimum delay difference between the DQS pin and the DQ pins:
			set_output_delay -min $data_output_min_delay -clock [get_clocks ${dqs_pin}_OUT ] [get_ports $dq_pin] -add_delay

			# Specifies the maximum delay difference between the DQS pin and the DQ pins:
			set_output_delay -max $data_output_max_delay -clock [get_clocks ${dqs_pin}_OUT ] [get_ports $dq_pin] -add_delay
		}
	}

	foreach { dqsn_pin } $dqsn_pins { dq_pins } $q_groups {
		foreach { dq_pin } $dq_pins {
			# Specifies the minimum delay difference between the DQS#pin and the DQ pins:
			set_output_delay -min $data_output_min_delay -clock [get_clocks ${dqsn_pin}_OUT ] [get_ports $dq_pin] -add_delay

			# Specifies the maximum delay difference between the DQS#pin and the DQ pins:
			set_output_delay -max $data_output_max_delay -clock [get_clocks ${dqsn_pin}_OUT ] [get_ports $dq_pin] -add_delay
		}
	}

	foreach dqs_out_clock_struct $dqs_out_clocks {
		array set dqs_out_clock $dqs_out_clock_struct

		if { [string length $dqs_out_clock(dm_pin)] > 0 } {
			# Specifies the minimum delay difference between the DQS and the DM pins:
			set_output_delay -min $data_output_min_delay -clock [get_clocks $dqs_out_clock(dst)_OUT ] [get_ports $dqs_out_clock(dm_pin)] -add_delay

			# Specifies the maximum delay difference between the DQS and the DM pins:
			set_output_delay -max $data_output_max_delay -clock [get_clocks $dqs_out_clock(dst)_OUT ] [get_ports $dqs_out_clock(dm_pin)] -add_delay
		}
	}

	foreach dqsn_out_clock_struct $dqsn_out_clocks {
		array set dqsn_out_clock $dqsn_out_clock_struct

		if { [string length $dqsn_out_clock(dm_pin)] > 0 } {
			# Specifies the minimum delay difference between the DQS and the DM pins:
			set_output_delay -min $data_output_min_delay -clock [get_clocks $dqsn_out_clock(dst)_OUT ] [get_ports $dqsn_out_clock(dm_pin)] -add_delay

			# Specifies the maximum delay difference between the DQS and the DM pins:
			set_output_delay -max $data_output_max_delay -clock [get_clocks $dqsn_out_clock(dst)_OUT ] [get_ports $dqsn_out_clock(dm_pin)] -add_delay
		}
	}

	##################
	#                #
	# DQS vs CK PATH #
	#                #
	##################

	foreach { ck_pin } $ck_pins { 
		set_output_delay -add_delay -clock [get_clocks $ck_pin] -max [nios_mem_if_ddr2_emif_0_p0_round_3dp [expr $t(CK) - $t(DQSS)*$t(CK) - $board(minCK_DQS_skew) ]] $dqs_pins
		set_output_delay -add_delay -clock [get_clocks $ck_pin] -min [nios_mem_if_ddr2_emif_0_p0_round_3dp [expr $t(DQSS)*$t(CK) - $board(maxCK_DQS_skew) ]] $dqs_pins
		set_false_path -to [get_clocks $ck_pin] -fall_from [get_clocks $local_pll_write_clk ] 
	}

	############
	#          #
	# A/C PATH #
	#          #
	############

	foreach { ck_pin } $ck_pins {
		# Specifies the minimum delay difference between the DQS pin and the address/control pins:
		set_output_delay -min $ac_min_delay -clock [get_clocks $ck_pin] [ get_ports $ac_pins ] -add_delay

		# Specifies the maximum delay difference between the DQS pin and the address/control pins:
		set_output_delay -max $ac_max_delay -clock [get_clocks $ck_pin] [ get_ports $ac_pins ] -add_delay
	}


	##########################
	#                        #
	# MULTICYCLE CONSTRAINTS #
	#                        #
	##########################

	# Relax timing for the AFI mux select signal. 
	# We don't assert the cal_done signal many cycles after we switch the AFI mux.
	set_multicycle_path -from [get_registers ${prefix}|*s0|*sequencer_phy_mgr_inst|phy_mux_sel] -to [remove_from_collection [get_keepers *] [get_registers ${prefix}|*s0|*sequencer_phy_mgr_inst|phy_mux_sel]] -setup 3
	set_multicycle_path -from [get_registers ${prefix}|*s0|*sequencer_phy_mgr_inst|phy_mux_sel] -to [remove_from_collection [get_keepers *] [get_registers ${prefix}|*s0|*sequencer_phy_mgr_inst|phy_mux_sel]] -hold 2

	# If powerdown feature is enabled, multicycle path from core logic to the CK generator. 
	# The PHY must be idle several cycles before entering and after exiting powerdown mode.
	if { [get_collection_size [get_registers -nowarn ${prefix}|*p0|*umemphy|*uio_pads|*uaddr_cmd_pads|*clock_gen[*].umem_ck_pad|*]] > 0 } {
		set_multicycle_path -to [get_registers ${prefix}|*p0|*umemphy|*uio_pads|*uaddr_cmd_pads|*clock_gen[*].umem_ck_pad|*] -end -setup 4
		set_multicycle_path -to [get_registers ${prefix}|*p0|*umemphy|*uio_pads|*uaddr_cmd_pads|*clock_gen[*].umem_ck_pad|*] -end -hold 4
	}
	



	set ac_fr_phase_mod_360 [expr round(2*$::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(3)) % 360]
	set ck_fr_phase_mod_360 [expr round($::GLOBAL_nios_mem_if_ddr2_emif_0_p0_pll_phase(1)) % 360]
	set ac_to_ck_setup_window_fr_degrees [expr {($ck_fr_phase_mod_360 + 360) - $ac_fr_phase_mod_360}]
	if {$ac_to_ck_setup_window_fr_degrees < 90} {
		foreach { ck_pin } $ck_pins {
			set_multicycle_path -from [get_clocks $local_pll_addr_cmd_clk] -to [get_clocks $ck_pin] -end -setup 2
			set_multicycle_path -from [get_clocks $local_pll_addr_cmd_clk] -to [get_clocks $ck_pin] -end -hold 0
		}
	}

	set read_fifo_read_dff ${prefix}|*p0|*altdq_dqs2_inst|*read_fifo~OUTPUT_DFF_*
	set read_fifo_write_address_dff ${prefix}|*p0|*altdq_dqs2_inst|*read_fifo~WRITE_ADDRESS_DFF
	set read_fifo_read_address_dff ${prefix}|*p0|*altdq_dqs2_inst|*read_fifo~READ_ADDRESS_DFF
	set lfifo_in_read_en_dff ${prefix}|*p0|*lfifo~LFIFO_IN_READ_EN_DFF
	set lfifo_in_read_en_full_dff ${prefix}|*p0|*lfifo~LFIFO_IN_READ_EN_FULL_DFF
	set lfifo_dff_reg ${prefix}|*p0|*lfifo~LFIFO_OUT_OCT_LFIFO_DFF
	set lfifo_out_rden_dff ${prefix}|*p0|*lfifo~LFIFO_OUT_RDEN_DFF
	set lfifo_out_rdata_valid_dff ${prefix}|*p0|*lfifo~LFIFO_OUT_RDATA_VALID_DFF
	set os_oct_ddio_oe_reg ${prefix}|*p0|*os_oct_ddio_oe~DFF
	set lfifo_rd_latency_dff ${prefix}|*p0|*lfifo~RD_LATENCY_DFF*
	set vfifo_qvld_in_dff ${prefix}|*p0|*altdq_dqs2_inst|vfifo~QVLD_IN_DFF
	set vfifo_inc_wr_ptr_dff ${prefix}|*p0|*altdq_dqs2_inst|vfifo~INC_WR_PTR_DFF
	set os_oe_reg ${prefix}|*p0|*os_oe_reg
	set phase_align_dff ${prefix}|*p0|*phase_align_os~DFF*
	set phy_read_latency_counter ${prefix}|*s0|*sequencer_phy_mgr_inst|phy_read_latency_counter\[*\]
	set phy_reset_mem_stable ${prefix}|*s0|*sequencer_phy_mgr_inst|phy_reset_mem_stable
	set read_fifo_reset ${prefix}|*s0|*sequencer_phy_mgr_inst|phy_read_fifo_reset\[*\]

	set core_to_fr_multicycle_setup 2
	set core_to_fr_multicycle_hold [expr {$core_to_fr_multicycle_setup - 1}]

	set after_u2b 0
	if {[get_collection_size [get_registers -nowarn $read_fifo_write_address_dff]] > 0} {
		set after_u2b 1
	}

	if {$after_u2b} {

		set_multicycle_path -from $phy_read_latency_counter -to $lfifo_rd_latency_dff -end -setup $core_to_fr_multicycle_setup
		set_multicycle_path -from $phy_read_latency_counter -to $lfifo_rd_latency_dff -end -hold $core_to_fr_multicycle_hold

		set_multicycle_path -from ${prefix}|*p0|*umemphy|*ureset|*ureset_afi_clk|reset_reg[*] -to $lfifo_in_read_en_dff -end -setup $core_to_fr_multicycle_setup
		set_multicycle_path -from ${prefix}|*p0|*umemphy|*ureset|*ureset_afi_clk|reset_reg[*] -to $lfifo_in_read_en_dff -end -hold $core_to_fr_multicycle_hold

		set_multicycle_path -from $phy_reset_mem_stable -to $vfifo_qvld_in_dff -end -setup $core_to_fr_multicycle_setup
		set_multicycle_path -from $phy_reset_mem_stable -to $vfifo_qvld_in_dff -end -hold $core_to_fr_multicycle_hold

		set_multicycle_path -from $read_fifo_reset -to $read_fifo_write_address_dff -end -setup 2
		set_multicycle_path -from $read_fifo_reset -to $read_fifo_write_address_dff -end -hold 1

		set_multicycle_path -from $read_fifo_reset -to $read_fifo_read_address_dff -end -setup 2
		set_multicycle_path -from $read_fifo_reset -to $read_fifo_read_address_dff -end -hold 1

		set_false_path -from $lfifo_out_rden_dff -to $read_fifo_read_address_dff

		set_false_path -from $pll_afi_phy_clock -to $lfifo_in_read_en_dff
		set_false_path -from $pll_afi_phy_clock -to $vfifo_qvld_in_dff
		set_false_path -from $pll_afi_phy_clock -to $os_oct_ddio_oe_reg
		set_false_path -from $pll_afi_phy_clock -to $os_oe_reg
		set_false_path -from $pll_afi_phy_clock -to $phase_align_dff
	}




	##########################
	#                        #
	# FALSE PATH CONSTRAINTS #
	#                        #
	##########################

	# Cut paths for memory clocks to avoid unconstrained warnings
	foreach { pin } [concat $dqsn_pins $ck_pins $ckn_pins] {
		set_false_path -to [get_ports $pin]
	}

	if { ! $synthesis_flow } {
		foreach dqs_in_clock_struct $dqs_in_clocks dqsn_out_clock_struct $dqsn_out_clocks {
			array set dqs_in_clock $dqs_in_clock_struct
			array set dqsn_out_clock $dqsn_out_clock_struct

			set_clock_groups -physically_exclusive	-group "$dqs_in_clock(dqs_pin)_IN" -group "$dqs_in_clock(dqs_pin)_OUT $dqsn_out_clock(dst)_OUT"

			# Cut paths between AFI Clock and Read Capture Registers
			set_false_path -from [get_clocks $local_pll_afi_clk] -to [get_clocks $dqs_in_clock(dqs_pin)_IN]

		}
	}

	foreach dqs_out_clock_struct $dqs_out_clocks {
		array set dqs_out_clock $dqs_out_clock_struct
		set_false_path -from $read_fifo_reset -to [ get_clocks $dqs_out_clock(dst)_OUT ]
	}

	# The paths between DQS_ENA_CLK and DQS_IN are calibrated, so they must not be analyzed
	set_false_path -from [get_clocks $local_pll_write_clk] -to [get_clocks {*_IN}]


	# The following registers serve as anchors for the pin_map.tcl
	# script and are not used by the IP during memory operation

	# Cut internal calibrated paths
	set dqs_delay_chain_pst_dff ${prefix}|*p0|*altdq_dqs2_inst|dqs_delay_chain~POSTAMBLE_DFF
	if {$after_u2b} {
		set_false_path -from ${prefix}|*p0|*altdq_dqs2_inst|dqs_enable_ctrl~* -to $dqs_delay_chain_pst_dff
	}

	# ------------------------------ #
	# -                            - #
	# --- FITTER OVERCONSTRAINTS --- #
	# -                            - #
	# ------------------------------ #
	if {$fit_flow} {

		set_clock_uncertainty -from [get_clocks $local_pll_afi_clk] -to [get_clocks $local_pll_avl_clock] -add -hold 0.200
		set_clock_uncertainty -from [get_clocks $local_pll_avl_clock] -to [get_clocks $local_pll_afi_clk] -add -hold 0.100
		set_clock_uncertainty -from [get_clocks $local_pll_afi_clk] -enable_same_physical_edge -add -hold 0.200
		set avl_writedata_avl ${prefix}|*s0|*rw_mgr_inst|*avl_writedata_avl*
		set avl_writedata_afi ${prefix}|*s0|*rw_mgr_inst|*avl_writedata_afi*
		if { ([get_collection_size [get_registers -nowarn $avl_writedata_avl]] > 0) && ([get_collection_size [get_registers -nowarn $avl_writedata_afi]] > 0) } {
			set_min_delay  -from $avl_writedata_avl -to $avl_writedata_afi 0.100
		}

	
		set_clock_uncertainty -from [get_clocks $local_pll_afi_clk] -to [get_clocks $local_pll_addr_cmd_clk] -add -setup 0.500
        set_clock_uncertainty -from [get_clocks $local_pll_afi_clk] -to [get_clocks $local_pll_write_clk] -add -hold 0.400
		set_clock_uncertainty -from [get_clocks $local_pll_avl_clock] -to [get_clocks $local_pll_afi_clk] -add -hold 0.100

        set_clock_uncertainty -from [get_clocks $local_pll_afi_clk] -to [get_clocks $local_pll_addr_cmd_clk] -add -hold 0.000

		if {[string compare -nocase $pll_afi_clock $pll_afi_phy_clock]==0} {
			set hr_to_fr_dff ${prefix}|*p0|*altdq_dqs2_inst|*hr_to_fr*
			if {[get_collection_size [get_registers -nowarn $hr_to_fr_dff]] > 0} {
				set_min_delay -to $hr_to_fr_dff 0.500
			}
		} else {
			set_clock_uncertainty -from [get_clocks $local_pll_afi_clk] -to [get_clocks $local_pll_afi_phy_clk] -add -hold 0.500
		}

	set_clock_uncertainty -from [get_clocks $local_pll_afi_phy_clk] -to [get_clocks $local_pll_afi_clk] -add -setup 0.800
	
	}
	
	# -------------------------------- #
	# -                              - #
	# --- TIMING MODEL ADJUSTMENTS --- #
	# -                              - #
	# -------------------------------- #

}

if {(($::quartus(nameofexecutable) ne "quartus_fit") && ($::quartus(nameofexecutable) ne "quartus_map"))} {
	set dqs_clocks [nios_mem_if_ddr2_emif_0_p0_get_all_instances_dqs_pins nios_mem_if_ddr2_emif_0_p0_ddr_db]
	# Leave clocks active when in debug mode
	if {[llength $dqs_clocks] > 0 && !$debug} {
		post_sdc_message info "Setting DQS clocks as inactive; use Report DDR to timing analyze DQS clocks"
		set_active_clocks [remove_from_collection [get_active_clocks] [get_clocks $dqs_clocks]]
	}
}

######################
#                    #
# REPORT DDR COMMAND #
#                    #
######################

add_ddr_report_command "source [list [file join [file dirname [info script]] ${::GLOBAL_nios_mem_if_ddr2_emif_0_p0_corename}_report_timing.tcl]]"

