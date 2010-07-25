onerror {resume}
quietly WaveActivateNextPane {} 0
add wave -noupdate -format Logic /tb_proc_control/mut/clock
add wave -noupdate -format Logic /tb_proc_control/mut/clock_en
add wave -noupdate -format Logic /tb_proc_control/mut/reset
add wave -noupdate -format Literal /tb_proc_control/mut/i_reg
add wave -noupdate -format Logic /tb_proc_control/mut/index_carry
add wave -noupdate -format Logic /tb_proc_control/mut/pc_carry
add wave -noupdate -format Logic /tb_proc_control/mut/branch_taken
add wave -noupdate -format Logic /tb_proc_control/mut/sync
add wave -noupdate -format Logic /tb_proc_control/mut/latch_dreg
add wave -noupdate -format Logic /tb_proc_control/mut/rwn
add wave -noupdate -format Literal /tb_proc_control/mut/a_mux
add wave -noupdate -format Literal /tb_proc_control/mut/dout_mux
add wave -noupdate -format Literal /tb_proc_control/mut/pc_oper
add wave -noupdate -format Literal /tb_proc_control/mut/s_oper
add wave -noupdate -format Literal /tb_proc_control/mut/adl_oper
add wave -noupdate -format Literal /tb_proc_control/mut/adh_oper
add wave -noupdate -format Literal /tb_proc_control/mut/state
add wave -noupdate -format Literal /tb_proc_control/mut/next_state
add wave -noupdate -format Logic /tb_proc_control/mut/next_rwn
add wave -noupdate -format Logic /tb_proc_control/mut/next_dreg
add wave -noupdate -format Literal /tb_proc_control/mut/next_amux
add wave -noupdate -format Literal /tb_proc_control/mut/next_dout
add wave -noupdate -format Literal /tb_proc_control/opcode
add wave -noupdate -format Literal /tb_proc_control/s_oper
add wave -noupdate -format Logic /tb_proc_control/s_is_absolute
add wave -noupdate -format Logic /tb_proc_control/s_is_abs_jump
add wave -noupdate -format Logic /tb_proc_control/s_is_immediate
add wave -noupdate -format Logic /tb_proc_control/s_is_implied
add wave -noupdate -format Logic /tb_proc_control/s_is_stack
add wave -noupdate -format Logic /tb_proc_control/s_is_push
add wave -noupdate -format Logic /tb_proc_control/s_is_zeropage
add wave -noupdate -format Logic /tb_proc_control/s_is_indirect
add wave -noupdate -format Logic /tb_proc_control/s_is_relative
add wave -noupdate -format Logic /tb_proc_control/s_is_load
add wave -noupdate -format Logic /tb_proc_control/s_is_store
add wave -noupdate -format Logic /tb_proc_control/s_is_rmw
add wave -noupdate -format Logic /tb_proc_control/s_is_jump
add wave -noupdate -format Logic /tb_proc_control/s_is_postindexed
add wave -noupdate -format Logic /tb_proc_control/s_store_a_from_alu
TreeUpdate [SetDefaultTree]
WaveRestoreCursors {{Cursor 1} {34450000 ps} 0}
configure wave -namecolwidth 150
configure wave -valuecolwidth 100
configure wave -justifyvalue left
configure wave -signalnamewidth 1
configure wave -snapdistance 10
configure wave -datasetprefix 0
configure wave -rowmargin 4
configure wave -childrowmargin 2
configure wave -gridoffset 0
configure wave -gridperiod 1
configure wave -griddelta 40
configure wave -timeline 0
update
WaveRestoreZoom {30991096 ps} {37908904 ps}
