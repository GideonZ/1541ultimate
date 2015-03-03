restart -f
#Load the program
mem load -infile ../../software/mb_boot/output_mb/icache_0.mem -format hex /mblite_sdram_tb/fpga/i_proc/i_proc/i_cache/i_cache_ram/ram
mem load -infile ../../software/mb_boot/output_mb/dcache_0.mem -format hex /mblite_sdram_tb/fpga/i_proc/i_proc/d_cache/i_cache_ram/ram
mem load -infile ../../software/mb_rtos/output/free_rtos_demo.m8 -format hex /mblite_sdram_tb/ram/mem_array
run 350 us
