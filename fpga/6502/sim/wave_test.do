onerror {resume}
quietly WaveActivateNextPane {} 0
add wave -noupdate -format Logic /tb_proc_core/core/clock
add wave -noupdate -format Logic /tb_proc_core/core/clock_en
add wave -noupdate -format Logic /tb_proc_core/core/reset
add wave -noupdate -format Literal /tb_proc_core/core/addr_out
add wave -noupdate -format Logic /tb_proc_core/core/rwn
add wave -noupdate -format Literal /tb_proc_core/core/data_out
add wave -noupdate -format Literal /tb_proc_core/core/data_in
add wave -noupdate -format Logic /tb_proc_core/core/read_write_n
add wave -noupdate -format Logic /tb_proc_core/core/index_carry
add wave -noupdate -format Logic /tb_proc_core/core/pc_carry
add wave -noupdate -format Logic /tb_proc_core/core/branch_taken
add wave -noupdate -format Literal /tb_proc_core/core/i_reg
add wave -noupdate -format Literal /tb_proc_core/core/d_reg
add wave -noupdate -format Literal /tb_proc_core/core/a_reg
add wave -noupdate -format Literal /tb_proc_core/core/x_reg
add wave -noupdate -format Literal /tb_proc_core/core/y_reg
add wave -noupdate -format Literal /tb_proc_core/core/s_reg
add wave -noupdate -format Literal /tb_proc_core/core/p_reg
add wave -noupdate -format Logic /tb_proc_core/core/latch_dreg
add wave -noupdate -format Logic /tb_proc_core/core/reg_update
add wave -noupdate -format Logic /tb_proc_core/core/copy_d2p
add wave -noupdate -format Logic /tb_proc_core/core/sync
add wave -noupdate -format Literal /tb_proc_core/core/a_mux
add wave -noupdate -format Literal /tb_proc_core/core/pc_oper
add wave -noupdate -format Literal /tb_proc_core/core/s_oper
add wave -noupdate -format Literal /tb_proc_core/core/adl_oper
add wave -noupdate -format Literal /tb_proc_core/core/adh_oper
add wave -noupdate -format Literal /tb_proc_core/core/dout_mux
add wave -noupdate -format Literal /tb_proc_core/core/alu_out
add wave -noupdate -format Literal /tb_proc_core/core/mem_out
add wave -noupdate -format Literal /tb_proc_core/core/impl_out
add wave -noupdate -format Logic /tb_proc_core/core/set_a
add wave -noupdate -format Logic /tb_proc_core/core/set_x
add wave -noupdate -format Logic /tb_proc_core/core/set_y
add wave -noupdate -format Logic /tb_proc_core/core/set_s
add wave -noupdate -format Literal /tb_proc_core/core/new_flags
add wave -noupdate -format Logic /tb_proc_core/core/n_out
add wave -noupdate -format Logic /tb_proc_core/core/v_out
add wave -noupdate -format Logic /tb_proc_core/core/c_out
add wave -noupdate -format Logic /tb_proc_core/core/z_out
add wave -noupdate -format Logic /tb_proc_core/core/d_out
add wave -noupdate -format Logic /tb_proc_core/core/i_out
add wave -noupdate -format Logic /tb_proc_core/core/interrupt
add wave -noupdate -format Literal /tb_proc_core/core/ctrl/state
add wave -noupdate -divider {New Divider}
add wave -noupdate -format Literal /tb_proc_core/core/oper/inst
add wave -noupdate -format Logic /tb_proc_core/core/oper/n_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/v_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/z_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/c_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/d_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/i_in
add wave -noupdate -format Literal /tb_proc_core/core/oper/data_in
add wave -noupdate -format Literal /tb_proc_core/core/oper/a_reg
add wave -noupdate -format Literal /tb_proc_core/core/oper/x_reg
add wave -noupdate -format Literal /tb_proc_core/core/oper/y_reg
add wave -noupdate -format Literal /tb_proc_core/core/oper/s_reg
add wave -noupdate -format Literal /tb_proc_core/core/oper/alu_out
add wave -noupdate -format Literal /tb_proc_core/core/oper/mem_out
add wave -noupdate -format Literal /tb_proc_core/core/oper/impl_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/set_a
add wave -noupdate -format Logic /tb_proc_core/core/oper/set_x
add wave -noupdate -format Logic /tb_proc_core/core/oper/set_y
add wave -noupdate -format Logic /tb_proc_core/core/oper/set_s
add wave -noupdate -format Logic /tb_proc_core/core/oper/n_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/v_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/z_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/c_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/d_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/i_out
add wave -noupdate -format Literal /tb_proc_core/core/oper/shift_sel
add wave -noupdate -format Literal /tb_proc_core/core/oper/shift_din
add wave -noupdate -format Literal /tb_proc_core/core/oper/shift_dout
add wave -noupdate -format Logic /tb_proc_core/core/oper/row0_n
add wave -noupdate -format Logic /tb_proc_core/core/oper/row0_v
add wave -noupdate -format Logic /tb_proc_core/core/oper/row0_z
add wave -noupdate -format Logic /tb_proc_core/core/oper/row0_c
add wave -noupdate -format Logic /tb_proc_core/core/oper/shft_n
add wave -noupdate -format Logic /tb_proc_core/core/oper/shft_z
add wave -noupdate -format Logic /tb_proc_core/core/oper/shft_c
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu_n
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu_v
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu_z
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu_c
add wave -noupdate -format Logic /tb_proc_core/core/oper/impl_n
add wave -noupdate -format Logic /tb_proc_core/core/oper/impl_z
add wave -noupdate -format Logic /tb_proc_core/core/oper/impl_c
add wave -noupdate -format Logic /tb_proc_core/core/oper/impl_v
add wave -noupdate -format Logic /tb_proc_core/core/oper/impl_i
add wave -noupdate -format Logic /tb_proc_core/core/oper/impl_d
add wave -noupdate -format Logic /tb_proc_core/core/oper/impl_en
add wave -noupdate -format Logic /tb_proc_core/core/oper/impl_flags
add wave -noupdate -divider ALU
add wave -noupdate -format Literal /tb_proc_core/core/oper/alu1/operation
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/enable
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/n_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/v_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/z_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/c_in
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/d_in
add wave -noupdate -format Literal /tb_proc_core/core/oper/alu1/data_a
add wave -noupdate -format Literal /tb_proc_core/core/oper/alu1/data_b
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/n_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/v_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/z_out
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/c_out
add wave -noupdate -format Literal /tb_proc_core/core/oper/alu1/data_out
add wave -noupdate -format Literal /tb_proc_core/core/oper/alu1/data_out_i
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/zero
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/sum_c
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/sum_n
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/sum_z
add wave -noupdate -format Logic /tb_proc_core/core/oper/alu1/sum_v
add wave -noupdate -format Literal /tb_proc_core/core/oper/alu1/sum_result
add wave -noupdate -format Literal /tb_proc_core/core/oper/alu1/oper4
add wave -noupdate -format Logic /tb_proc_core/core/intr/clock
add wave -noupdate -format Logic /tb_proc_core/core/intr/clock_en
add wave -noupdate -format Logic /tb_proc_core/core/intr/reset
add wave -noupdate -format Logic /tb_proc_core/core/intr/irq_n
add wave -noupdate -format Logic /tb_proc_core/core/intr/nmi_n
add wave -noupdate -format Logic /tb_proc_core/core/intr/i_flag
add wave -noupdate -format Logic /tb_proc_core/core/intr/vect_bit
add wave -noupdate -format Logic /tb_proc_core/core/intr/interrupt
add wave -noupdate -format Literal /tb_proc_core/core/intr/vect_addr
add wave -noupdate -format Logic /tb_proc_core/core/intr/irq_c
add wave -noupdate -format Logic /tb_proc_core/core/intr/nmi_c
add wave -noupdate -format Logic /tb_proc_core/core/intr/nmi_d
add wave -noupdate -format Logic /tb_proc_core/core/intr/nmi_act
add wave -noupdate -format Literal /tb_proc_core/core/intr/vect_h
add wave -noupdate -format Literal /tb_proc_core/core/intr/state
TreeUpdate [SetDefaultTree]
WaveRestoreCursors {{Cursor 1} {53180956 ps} 0}
configure wave -namecolwidth 150
configure wave -valuecolwidth 101
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
WaveRestoreZoom {0 ps} {105 us}
