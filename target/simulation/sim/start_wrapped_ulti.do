restart -f
#Load the program
mem load -infile ../../software/mb_sim_boot/output/mb_boot.s00 -format hex /mblite_simu_wrapped/memory
mem load -infile ../../software/mb_ultimate/output/ultimate.sim -format hex /mblite_simu_wrapped/memory
run 1 us

