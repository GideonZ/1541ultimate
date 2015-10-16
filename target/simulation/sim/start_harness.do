restart -f
#Load the program
mem load -infile ../../software/mb_boot/output/mb_boot.m32 -format hex /harness_logic_32/mut/i_cpu/i_proc/d_cache/i_cache_ram/ram
mem load -infile ../../software/mb_boot/output/mb_boot.m32 -format hex /harness_logic_32/mut/i_cpu/i_proc/i_cache/i_cache_ram/ram
mem load -infile ../../software/mb_boot2/output/mb_boot2.sim -format hex /harness_logic_32/ram/mem_array
mem load -infile ../../software/mb_ultimate/output/ultimate.sim -format hex /harness_logic_32/ram/mem_array

run 200 us
