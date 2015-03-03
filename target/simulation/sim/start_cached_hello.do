restart -f
#Load the program
mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /mblite_simu_cached/i_core/i_cache/i_cache_ram/ram
mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /mblite_simu_cached/i_core/d_cache/i_cache_ram/ram
mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /mblite_simu_cached/memory
mem load -infile ../../software/mb_hello/output_mb/hello.mem -format hex /mblite_simu_cached/memory
run 1 us
