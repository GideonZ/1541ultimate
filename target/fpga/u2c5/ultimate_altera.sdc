#**************************************************************
# Time Information
#**************************************************************

set_time_format -unit ns -decimal_places 3



#**************************************************************
# Create Clock
#**************************************************************

create_clock -name {altera_reserved_tck} -period 100.000 -waveform { 0.000 50.000 } [get_ports {altera_reserved_tck}]
create_clock -name {RMII_REFCLK} -period 20.000 -waveform { 0.000 10.000 } [get_ports {RMII_REFCLK}]
create_clock -name {ULPI_CLOCK} -period 16.500 -waveform { 0.000 8.250 } [get_ports {ULPI_CLOCK}]


#**************************************************************
# Create Generated Clock
#**************************************************************



#**************************************************************
# USB timing
#**************************************************************
    
# Set Input Delay
set_input_delay -add_delay -clock [get_clocks ULPI_CLOCK] -max 1.5 [get_ports -no_case "ULPI_DATA* ULPI_DIR ULPI_NXT"]
set_input_delay -add_delay -clock [get_clocks ULPI_CLOCK] -min 3.5 [get_ports -no_case "ULPI_DATA* ULPI_DIR ULPI_NXT"]

# Set Output Delay
# max output delay (measured from next clock edge) = set_up of other device.
set_output_delay -add_delay -max -clock [get_clocks ULPI_CLOCK]  5.0 [get_ports -no_case "ULPI_DATA* ULPI_STP"]
set_output_delay -add_delay -min -clock [get_clocks ULPI_CLOCK]  0   [get_ports -no_case "ULPI_DATA* ULPI_STP"]

# Set a multicycle path on the bus turnaround
# set_multicycle_path -setup -end -from [get_keepers -no_case "ULPI_DIR"] -to [get_keepers -no_case "ULPI_DATA*"] 2
# set_multicycle_path -hold -end -from [get_keepers -no_case "ULPI_DIR"] -to [get_keepers -no_case "ULPI_DATA*"] 2


#**************************************************************
# Set Clock Latency
#**************************************************************



#**************************************************************
# Set Clock Uncertainty
#**************************************************************



#**************************************************************
# Set Input Delay
#**************************************************************



#**************************************************************
# Set Output Delay
#**************************************************************



#**************************************************************
# Set Clock Groups
#**************************************************************

set_clock_groups -asynchronous -group [get_clocks {altera_reserved_tck}] 


#**************************************************************
# Set False Path
#**************************************************************

#set_false_path  -from  [get_clocks {RMII_REFCLK}] -to  [get_clocks {i_nios|mem_if_ddr2_emif_0|pll0|pll_afi_clk}]
set_false_path  -from  [get_clocks {ULPI_CLOCK}]  -to  [get_clocks {i_nios|mem_if_ddr2_emif_0|pll0|pll_afi_clk}]
set_false_path  -to    [get_clocks {ULPI_CLOCK}] -from [get_clocks {i_nios|mem_if_ddr2_emif_0|pll0|pll_afi_clk}]

set_false_path -from [all_registers] -to [get_keepers {SDRAM_CKE SDRAM_ODT SDRAM_CSn {SDRAM_A[0]} {SDRAM_A[10]} {SDRAM_A[11]} {SDRAM_A[12]} {SDRAM_A[13]} {SDRAM_A[1]} {SDRAM_A[2]} {SDRAM_A[3]} {SDRAM_A[4]} {SDRAM_A[5]} {SDRAM_A[6]} {SDRAM_A[7]} {SDRAM_A[8]} {SDRAM_A[9]} {SDRAM_BA[0]} {SDRAM_BA[1]} SDRAM_CASn SDRAM_RASn SDRAM_WEn}]

set_false_path -from [get_keepers {nios:i_nios|nios_pio_0:pio_0|data_out[15]}] -to [get_keepers {\b_audio:stream_in_*}]


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

