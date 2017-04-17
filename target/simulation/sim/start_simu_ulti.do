restart -f
#Load the program
mem load -infile ../../software/mb_sim_boot/output/mb_boot.s00 -format hex /mblite_simu/memory
#mem load -infile ../../software/mb_ultimate/output/ultimate.sim -format hex /mblite_simu/memory
#mem load -infile ../../software/mb_rtos/output/free_rtos_demo.sim -format hex /mblite_simu/memory
mem load -infile ../../software/mb_boot2/output/mb_boot2.sim -format hex /mblite_simu/memory
run 1 us
