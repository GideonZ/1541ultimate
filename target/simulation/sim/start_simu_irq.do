restart -f
#Load the program
mem load -infile ../../software/mb_sim_boot/output/mb_boot.s00 -format hex /mblite_simu/memory
mem load -infile ../../software/mb_irq/output/irq_test.sim -format hex /mblite_simu/memory
run 1 us
