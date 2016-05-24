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
# This file specifies the timing properties of the memory device and
# of the memory interface


package require ::quartus::ddr_timing_model

###################
#                 #
# TIMING SETTINGS #
#                 #
###################

# Interface Clock Period
set t(CK) 5.0

# Reference Clock Period
set t(refCK) 20.0

# Minimum Clock Period
set t(min_CK) 2.5

##########################
# Memory timing parameters
##########################

# A/C Setup/Hold
set t(IS) 0.375
set t(IH) 0.375

# Data Setup/Hold
set t(DS) 0.25
set t(DH) 0.25

# DQS clock edge to DQ data edge (in same group)
set t(DQSQ) [expr { 200 / 1000.0 }]
set t(QHS) [expr { 300 / 1000.0 }]

# Convert QH into time unit so that it's consistent with DQSQ
set t(QH_time) [expr 0.5 * $t(CK) - $t(QHS) ]

# DQS to CK input timing
set t(DSS) 0.2
set t(DSH) 0.2
set t(DQSS) 0.25
set t(DSS) [expr $t(DSS)*$t(min_CK)/$t(CK)]
set t(DSH) [expr $t(DSH)*$t(min_CK)/$t(CK)]
set t(DQSS) [expr 0.5 - $t(DQSS)*$t(min_CK)/$t(CK)]

# DQS Timing
set t(DQSH) 0.35

# Write Levelling parameters
set t(WLS) [ expr 0.13 * $t(min_CK) ]
set t(WLH) [ expr 0.13 * $t(min_CK) ]

# DQS to CK timing on reads
set t(DQSCK) [expr { 350 / 1000.0 }]

# FPGA Duty Cycle Distortion
set t(DCD) 0.0

#######################
# Controller parameters
#######################

set t(RL) 3
set t(WL) 2
set t(DWIDTH_RATIO) [expr { 2 * 2 }]
set t(rd_to_wr_turnaround_oct) 3

#####################
# FPGA specifications
#####################

# Sequencer VCALIB width. Determins multicycle length
set vcalib_count_width 2

set fpga(tPLL_PSERR) 0.0
set fpga(tPLL_JITTER) 0.0

# Systematic DCD in the Write Levelling delay chains
set t(WL_DCD) [expr [get_micro_node_delay -micro WL_DCD -parameters {IO VPAD} -in_fitter]/1000.0]
# Non-systematic DC jitter in the Write Levelling delay chains
set t(WL_DCJ) [expr [get_micro_node_delay -micro WL_DC_JITTER -parameters {IO VPAD} -in_fitter]/1000.0]
# Phase shift error in the Write Levelling delay chains between DQ and DQS
set t(WL_PSE) 0.0
# Jitter in the Write Levelling delay chains
set t(WL_JITTER) [expr [get_micro_node_delay -micro WL_JITTER -parameters {IO PHY_SHORT} -in_fitter]/1000.0]
set t(WL_JITTER_DIVISION) [expr [get_micro_node_delay -micro WL_JITTER_DIVISION -parameters {IO PHY_SHORT} -in_fitter]/100.0]

###############
# SSN Info
###############

set SSN(pushout_o) [expr [get_micro_node_delay -micro SSO -parameters [list IO DQDQSABSOLUTE NONLEVELED MAX] -in_fitter]/1000.0]
set SSN(pullin_o)  [expr [get_micro_node_delay -micro SSO -parameters [list IO DQDQSABSOLUTE NONLEVELED MIN] -in_fitter]/-1000.0]
set SSN(pushout_i) [expr [get_micro_node_delay -micro SSI -parameters [list IO DQDQSABSOLUTE NONLEVELED MAX] -in_fitter]/1000.0]
set SSN(pullin_i)  [expr [get_micro_node_delay -micro SSI -parameters [list IO DQDQSABSOLUTE NONLEVELED MIN] -in_fitter]/-1000.0]
set SSN(rel_pushout_o) [expr [get_micro_node_delay -micro SSO -parameters [list IO DQDQSRELATIVE NONLEVELED MAX] -in_fitter]/1000.0]
set SSN(rel_pullin_o)  [expr [get_micro_node_delay -micro SSO -parameters [list IO DQDQSRELATIVE NONLEVELED MIN] -in_fitter]/-1000.0]
set SSN(rel_pushout_i) [expr [get_micro_node_delay -micro SSI -parameters [list IO DQDQSRELATIVE NONLEVELED MAX] -in_fitter]/1000.0]
set SSN(rel_pullin_i)  [expr [get_micro_node_delay -micro SSI -parameters [list IO DQDQSRELATIVE NONLEVELED MIN] -in_fitter]/-1000.0]

###############
# Board Effects
###############

# Intersymbol Interference
set ISI(addresscmd_setup) 0.0
set ISI(addresscmd_hold) 0.0
set ISI(DQ) 0.0
set ISI(DQS) 0.0
set ISI(READ_DQ) 0.0
set ISI(READ_DQS) 0.0

# Board skews
set board(abs_max_CK_delay) 0.6
set board(abs_max_DQS_delay) 0.6
set board(minCK_DQS_skew) -0.01
set board(maxCK_DQS_skew) 0.01
set board(tpd_inter_DIMM) 0.0
set board(intra_DQS_group_skew) 0.02
set board(inter_DQS_group_skew) 0.02
set board(DQ_DQS_skew) 0.0
set board(intra_addr_ctrl_skew) 0.02
set board(addresscmd_CK_skew) 0.0

