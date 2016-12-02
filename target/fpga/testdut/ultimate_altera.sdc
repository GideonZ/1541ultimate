## Generated SDC file "ultimate_altera.sdc"

## Copyright (C) 1991-2015 Altera Corporation. All rights reserved.
## Your use of Altera Corporation's design tools, logic functions 
## and other software and tools, and its AMPP partner logic 
## functions, and any output files from any of the foregoing 
## (including device programming or simulation files), and any 
## associated documentation or information are expressly subject 
## to the terms and conditions of the Altera Program License 
## Subscription Agreement, the Altera Quartus Prime License Agreement,
## the Altera MegaCore Function License Agreement, or other 
## applicable license agreement, including, without limitation, 
## that your use is for the sole purpose of programming logic 
## devices manufactured by Altera and sold by Altera or its 
## authorized distributors.  Please refer to the applicable 
## agreement for further details.


## VENDOR  "Altera"
## PROGRAM "Quartus Prime"
## VERSION "Version 15.1.0 Build 185 10/21/2015 SJ Lite Edition"

## DATE    "Sat Jul 30 09:13:02 2016"

##
## DEVICE  "EP4CE22F17C8"
##


#**************************************************************
# Time Information
#**************************************************************

set_time_format -unit ns -decimal_places 3



#**************************************************************
# Create Clock
#**************************************************************

create_clock -name {altera_reserved_tck} -period 100.000 -waveform { 0.000 50.000 } [get_ports {altera_reserved_tck}]
create_clock -name {ref_clock} -period 20.000 -waveform { 0.000 10.000 } [get_ports {RMII_REFCLK}]
create_clock -name {ulpi_clock} -period 16.666 -waveform { 0.000 8.333 } [get_ports {ULPI_CLOCK}]
derive_pll_clocks
set sys_clock "i_memphy|i_phy|i_pll|auto_generated|pll1|clk[0]"

#**************************************************************
# Create Generated Clock
#**************************************************************



#**************************************************************
# Set Clock Latency
#**************************************************************



#**************************************************************
# Set Clock Uncertainty
#**************************************************************

set_clock_uncertainty -from { altera_reserved_tck } -to { altera_reserved_tck } 0.1
set_clock_uncertainty -from { ref_clock } -to { ref_clock } 0.1
set_clock_uncertainty -from { ulpi_clock } -to { ulpi_clock } 0.1

derive_clock_uncertainty

#**************************************************************
# Set Input Delay
#**************************************************************



#**************************************************************
# Set Output Delay
#**************************************************************



#**************************************************************
# Set Clock Groups
#**************************************************************

set_clock_groups -asynchronous -group [get_clocks {altera_reserved_tck $sys_clock ulpi_clock ref_clock}] 


#**************************************************************
# Set False Path
#**************************************************************

set_false_path  -to  [get_keepers {*_tig}]
set_false_path  -to  [get_keepers {*synchroniser:*_tig*}]
set_false_path  -to  [get_keepers {*synchronizer_gzw:*_tig*}]
set_false_path  -to  [get_keepers {*level_synchronizer:*|sync1}]
set_false_path  -to  [get_keepers {*pulse_synchronizer:*|sync1}]

set_false_path -to [get_keepers {*altera_std_synchronizer:*|din_s1}]
set_false_path -from [get_keepers {**}] -to [get_keepers {*phasedone_state*}]
set_false_path -from [get_keepers {**}] -to [get_keepers {*internal_phasestep*}]
set_false_path -from [get_keepers {*rdptr_g*}] -to [get_keepers {*ws_dgrp|dffpipe_se9:dffpipe15|dffe16a*}]
set_false_path -from [get_keepers {*delayed_wrptr_g*}] -to [get_keepers {*rs_dgwp|dffpipe_re9:dffpipe12|dffe13a*}]
set_false_path -from [get_keepers {*rdptr_g*}] -to [get_keepers {*ws_dgrp|dffpipe_qe9:dffpipe15|dffe16a*}]
set_false_path -from [get_keepers {*delayed_wrptr_g*}] -to [get_keepers {*rs_dgwp|dffpipe_pe9:dffpipe12|dffe13a*}]
set_false_path -from [get_keepers {*rdptr_g*}] -to [get_keepers {*ws_dgrp|dffpipe_dd9:dffpipe15|dffe16a*}]
set_false_path -from [get_keepers {*delayed_wrptr_g*}] -to [get_keepers {*rs_dgwp|dffpipe_cd9:dffpipe12|dffe13a*}]
set_false_path -to [get_pins -nocase -compatibility_mode {*|alt_rst_sync_uq1|altera_reset_synchronizer_int_chain*|clrn}]
set_false_path -from [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_nios2_oci_break:the_nios_solo_nios2_gen2_0_cpu_nios2_oci_break|break_readreg*}] -to [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper:the_nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper|nios_solo_nios2_gen2_0_cpu_debug_slave_tck:the_nios_solo_nios2_gen2_0_cpu_debug_slave_tck|*sr*}]
set_false_path -from [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_nios2_oci_debug:the_nios_solo_nios2_gen2_0_cpu_nios2_oci_debug|*resetlatch}] -to [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper:the_nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper|nios_solo_nios2_gen2_0_cpu_debug_slave_tck:the_nios_solo_nios2_gen2_0_cpu_debug_slave_tck|*sr[33]}]
set_false_path -from [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_nios2_oci_debug:the_nios_solo_nios2_gen2_0_cpu_nios2_oci_debug|monitor_ready}] -to [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper:the_nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper|nios_solo_nios2_gen2_0_cpu_debug_slave_tck:the_nios_solo_nios2_gen2_0_cpu_debug_slave_tck|*sr[0]}]
set_false_path -from [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_nios2_oci_debug:the_nios_solo_nios2_gen2_0_cpu_nios2_oci_debug|monitor_error}] -to [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper:the_nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper|nios_solo_nios2_gen2_0_cpu_debug_slave_tck:the_nios_solo_nios2_gen2_0_cpu_debug_slave_tck|*sr[34]}]
set_false_path -from [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_nios2_ocimem:the_nios_solo_nios2_gen2_0_cpu_nios2_ocimem|*MonDReg*}] -to [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper:the_nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper|nios_solo_nios2_gen2_0_cpu_debug_slave_tck:the_nios_solo_nios2_gen2_0_cpu_debug_slave_tck|*sr*}]
set_false_path -from [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper:the_nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper|nios_solo_nios2_gen2_0_cpu_debug_slave_tck:the_nios_solo_nios2_gen2_0_cpu_debug_slave_tck|*sr*}] -to [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper:the_nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper|nios_solo_nios2_gen2_0_cpu_debug_slave_sysclk:the_nios_solo_nios2_gen2_0_cpu_debug_slave_sysclk|*jdo*}]
set_false_path -from [get_keepers {sld_hub:*|irf_reg*}] -to [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper:the_nios_solo_nios2_gen2_0_cpu_debug_slave_wrapper|nios_solo_nios2_gen2_0_cpu_debug_slave_sysclk:the_nios_solo_nios2_gen2_0_cpu_debug_slave_sysclk|ir*}]
set_false_path -from [get_keepers {sld_hub:*|sld_shadow_jsm:shadow_jsm|state[1]}] -to [get_keepers {*nios_solo_nios2_gen2_0_cpu:*|nios_solo_nios2_gen2_0_cpu_nios2_oci:the_nios_solo_nios2_gen2_0_cpu_nios2_oci|nios_solo_nios2_gen2_0_cpu_nios2_oci_debug:the_nios_solo_nios2_gen2_0_cpu_nios2_oci_debug|monitor_go}]

set_false_path -from {ddr2_ctrl:i_memphy|mem_io:i_phy|sys_reset_pipe[0]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|altddio_out:i_dqs|ddio_out_dih:auto_generated|ddio_outa[0]~DFFHI}
set_false_path -from {ddr2_ctrl:i_memphy|mem_io:i_phy|sys_reset_pipe[0]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|altddio_out:i_dqs|ddio_out_dih:auto_generated|ddio_outa[0]~DFFLO}
set_false_path -from {ddr2_ctrl:i_memphy|mem_io:i_phy|sys_reset_pipe[0]} -to {async_fifo_ft:\b_audio:i_aout|dcfifo:dcfifo_component|dcfifo_gpm1:auto_generated|dffpipe_3dc:rdaclr|dffe1*}
set_false_path -from {level_synchronizer:i_audio_reset|sync2} -to {async_fifo_ft:\b_audio:i_aout|dcfifo:dcfifo_component|dcfifo_gpm1:auto_generated|dffpipe_3dc:wraclr|dffe1*}

#**************************************************************
# USB timing
#**************************************************************
    
# Set Input Delay
set_input_delay -add_delay -clock [get_clocks ulpi_clock] -max 1.5 [get_ports -no_case "ULPI_DATA* ULPI_DIR ULPI_NXT"]
set_input_delay -add_delay -clock [get_clocks ulpi_clock] -min 3.5 [get_ports -no_case "ULPI_DATA* ULPI_DIR ULPI_NXT"]

# Set Output Delay
# max output delay (measured from next clock edge) = set_up of other device.
set_output_delay -add_delay -max -clock [get_clocks ulpi_clock]  5.0 [get_ports -no_case "ULPI_DATA* ULPI_STP"]
set_output_delay -add_delay -min -clock [get_clocks ulpi_clock]  0   [get_ports -no_case "ULPI_DATA* ULPI_STP"]

# Set a multicycle path on the bus turnaround
# set_multicycle_path -setup -end -from [get_keepers -no_case "ULPI_DIR"] -to [get_keepers -no_case "ULPI_DATA*"] 2
# set_multicycle_path -hold -end -from [get_keepers -no_case "ULPI_DIR"] -to [get_keepers -no_case "ULPI_DATA*"] 2

#**************************************************************
# Set Multicycle Path
#**************************************************************



#**************************************************************
# Set Maximum Delay
#**************************************************************



#**************************************************************
# Set Minimum Delay
#**************************************************************



#**************************************************************
# Set Input Transition
#**************************************************************

