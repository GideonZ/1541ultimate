restart -f
#Load the program

mem load -infile ../../software/mb_sim_boot/output_mb/mb_boot.mem -format hex /mb_model_tb/model/imem
mem load -infile ../../software/mb_hello/output_mb/hello.mem -format hex /mb_model_tb/model/imem

#run 100 ns
#mem save -o ../../software/mb_hello/dump0.mem -f hex /mb_model_tb/model/imem -start 16384 -end 24000 -words 4
#run 100 us
#mem save -o ../../software/mb_hello/dump1.mem -f hex /mb_model_tb/model/imem -start 16384 -end 24000 -words 4
#run 100 us
#mem save -o ../../software/mb_hello/dump2.mem -f hex /mb_model_tb/model/imem -start 16384 -end 24000 -words 4
#run 100 us
#mem save -o ../../software/mb_hello/dump3.mem -f hex /mb_model_tb/model/imem -start 16384 -end 24000 -words 4
#run 100 us
#mem save -o ../../software/mb_hello/dump4.mem -f hex /mb_model_tb/model/imem -start 16384 -end 24000 -words 4
#run 1.5 ms
#mem save -o ../../software/mb_hello/dump5.mem -f hex /mb_model_tb/model/imem -start 16384 -end 24000 -words 4
