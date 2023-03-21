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

create_generated_clock -name DDR2CLK -invert -source [get_pins {i_memphy|i_phy|i_clk_p|auto_generated|ddio_outa[0]|muxsel}] [get_ports SDRAM_CLK]
#create_generated_clock -name DDR2DQS -phase 180 -source [get_pins {i_memphy|i_phy|i_clk_p|auto_generated|ddio_outa[0]|muxsel}] [get_ports SDRAM_CLK]


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
# Set Input Delay   (= clock to out of receiving device)
#**************************************************************

set ddr2_read_clock "i_memphy|i_phy|i_pll|auto_generated|pll1|clk[3]"

set_false_path -setup -fall_from [get_clocks DDR2CLK] -rise_to [get_clocks $ddr2_read_clock]
set_false_path -setup -rise_from [get_clocks DDR2CLK] -fall_to [get_clocks $ddr2_read_clock]
set_false_path -hold -rise_from [get_clocks DDR2CLK] -rise_to [get_clocks $ddr2_read_clock]
set_false_path -hold -fall_from [get_clocks DDR2CLK] -fall_to [get_clocks $ddr2_read_clock]

set_false_path -setup -fall_from [get_clocks $ddr2_read_clock] -rise_to [get_clocks $sys_clock]

# Assume 100 ps PCB flight time
set_input_delay -add_delay -max 0.7  -clock [get_clocks DDR2CLK] [get_ports -no_case "SDRAM_DQ[*] SDRAM_DM"]
set_input_delay -add_delay -min -0.5 -clock [get_clocks DDR2CLK] [get_ports -no_case "SDRAM_DQ[*] SDRAM_DM"]
set_input_delay -add_delay -max 0.7  -clock [get_clocks DDR2CLK] -clock_fall [get_ports -no_case "SDRAM_DQ[*] SDRAM_DM"]
set_input_delay -add_delay -min -0.5 -clock [get_clocks DDR2CLK] -clock_fall [get_ports -no_case "SDRAM_DQ[*] SDRAM_DM"]

#**************************************************************
# Set Output Delay  (= setup of receiving device)
#**************************************************************

set_output_delay -add_delay -max  0.3 -clock [get_clocks DDR2CLK]   [get_ports -no_case "SDRAM_A* SDRAM_BA* SDRAM_RASn SDRAM_CASn SDRAM_WEn"]
set_output_delay -add_delay -min -0.3 -clock [get_clocks DDR2CLK]   [get_ports -no_case "SDRAM_A* SDRAM_BA* SDRAM_RASn SDRAM_CASn SDRAM_WEn"]


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
set_false_path -from {ddr2_ctrl:i_memphy|mem_io:i_phy|sys_reset_pipe[0]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|altddio_out:i_dqs|ddio_out_dih:auto_generated|dffe1a[0]}


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

set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[0]} -to {SDRAM_DQ[0]~output} 3.6
set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[1]} -to {SDRAM_DQ[1]~output} 3.6
set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[2]} -to {SDRAM_DQ[2]~output} 3.6
set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[3]} -to {SDRAM_DQ[3]~output} 3.6
set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[4]} -to {SDRAM_DQ[4]~output} 3.6
set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[5]} -to {SDRAM_DQ[5]~output} 3.6
set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[6]} -to {SDRAM_DQ[6]~output} 3.6
set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[7]} -to {SDRAM_DQ[7]~output} 3.6
set_net_delay -max -from {i_memphy|i_phy|i_data|auto_generated|dffe1a[8]} -to {SDRAM_DM~output} 3.6

set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[0]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[0]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[1]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[1]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[2]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[2]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[3]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[3]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[4]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[4]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[5]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[5]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[6]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[6]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[7]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[7]} 1.0
#set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[8]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[8]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[9]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[9]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[10]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[10]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[11]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[11]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[12]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[12]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[13]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[13]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[14]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[14]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[15]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[15]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[16]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[16]} 1.0
#set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[17]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[17]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[18]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[18]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[19]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[19]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[20]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[20]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[21]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[21]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[22]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[22]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[23]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[23]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[24]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[24]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[25]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[25]} 1.0
#set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[26]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[26]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[27]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[27]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[28]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[28]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[29]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[29]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[30]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[30]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[31]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[31]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[32]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[32]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[33]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[33]} 1.0
set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[34]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[34]} 1.0
#set_net_delay -max -from {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata_32[35]} -to {ddr2_ctrl:i_memphy|mem_io:i_phy|rdata[35]} 1.0

#**************************************************************
# Set Minimum Delay
#**************************************************************



#**************************************************************
# Set Input Transition
#**************************************************************

