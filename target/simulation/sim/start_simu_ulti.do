restart -f
#Load the program
mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /mblite_simu/memory
mem load -infile ../../software/mb_ultimate/output/ultimate.mem -format hex /mblite_simu/memory
run 1 us
