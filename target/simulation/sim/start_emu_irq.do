restart -f
#Load the program

mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /mb_model_tb/model/imem
mem load -infile ../../software/mb_irq/output_mb/irq_test.mem -format hex /mb_model_tb/model/imem
