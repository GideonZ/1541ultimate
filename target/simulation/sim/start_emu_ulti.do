restart -f
#Load the program

mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /mb_model_tb/model/imem
mem load -infile ../../software/mb_ultimate/output/ultimate.mem -format hex /mb_model_tb/model/imem

