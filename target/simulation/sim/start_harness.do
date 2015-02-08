restart -f
#Load the program
mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /harness_logic_32/mut/i_cpu/i_proc/d_cache/i_cache_ram/ram
mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /harness_logic_32/mut/i_cpu/i_proc/i_cache/i_cache_ram/ram
mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.m8 -format hex /harness_logic_32/ram/mem_array
mem load -infile ../../software/mb_usb_test/output_mb/usb_test.m8 -format hex /harness_logic_32/ram/mem_array
run 50 us
