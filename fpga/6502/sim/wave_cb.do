onerror {resume}
quietly WaveActivateNextPane {} 0
add wave -noupdate -format Logic /tb_proc_core/clock
add wave -noupdate -format Logic /tb_proc_core/clock_en
add wave -noupdate -format Logic /tb_proc_core/reset
add wave -noupdate -format Literal /tb_proc_core/addr_out
add wave -noupdate -format Literal /tb_proc_core/data_in
add wave -noupdate -format Literal /tb_proc_core/data_out
add wave -noupdate -format Logic /tb_proc_core/read_write_n
add wave -noupdate -format Logic /tb_proc_core/nmi_n
add wave -noupdate -format Logic /tb_proc_core/irq_n
add wave -noupdate -format Logic /tb_proc_core/stop_clock
add wave -noupdate -divider {New Divider}
add wave -noupdate -format Logic /tb_proc_core/core/regs/clock
add wave -noupdate -format Logic /tb_proc_core/core/regs/clock_en
add wave -noupdate -format Logic /tb_proc_core/core/regs/reset
add wave -noupdate -format Literal /tb_proc_core/core/regs/data_in
add wave -noupdate -format Literal /tb_proc_core/core/regs/data_out
add wave -noupdate -format Logic /tb_proc_core/core/regs/so_n
add wave -noupdate -format Literal /tb_proc_core/core/regs/alu_data
add wave -noupdate -format Literal /tb_proc_core/core/regs/mem_data
add wave -noupdate -format Literal /tb_proc_core/core/regs/new_flags
add wave -noupdate -format Logic /tb_proc_core/core/regs/set_a
add wave -noupdate -format Logic /tb_proc_core/core/regs/set_x
add wave -noupdate -format Logic /tb_proc_core/core/regs/set_y
add wave -noupdate -format Logic /tb_proc_core/core/regs/set_s
add wave -noupdate -format Literal /tb_proc_core/core/regs/set_data
add wave -noupdate -format Logic /tb_proc_core/core/regs/interrupt
add wave -noupdate -format Literal /tb_proc_core/core/regs/vect_addr
add wave -noupdate -format Logic /tb_proc_core/core/regs/set_b
add wave -noupdate -format Logic /tb_proc_core/core/regs/clear_b
add wave -noupdate -format Logic /tb_proc_core/core/regs/sync
add wave -noupdate -format Logic /tb_proc_core/core/regs/latch_dreg
add wave -noupdate -format Logic /tb_proc_core/core/regs/vect_bit
add wave -noupdate -format Logic /tb_proc_core/core/regs/reg_update
add wave -noupdate -format Logic /tb_proc_core/core/regs/copy_d2p
add wave -noupdate -format Literal /tb_proc_core/core/regs/a_mux
add wave -noupdate -format Literal /tb_proc_core/core/regs/dout_mux
add wave -noupdate -format Literal /tb_proc_core/core/regs/pc_oper
add wave -noupdate -format Literal /tb_proc_core/core/regs/s_oper
add wave -noupdate -format Literal /tb_proc_core/core/regs/adl_oper
add wave -noupdate -format Literal /tb_proc_core/core/regs/adh_oper
add wave -noupdate -format Literal /tb_proc_core/core/regs/i_reg
add wave -noupdate -format Logic /tb_proc_core/core/regs/index_carry
add wave -noupdate -format Logic /tb_proc_core/core/regs/pc_carry
add wave -noupdate -format Logic /tb_proc_core/core/regs/branch_taken
add wave -noupdate -format Literal /tb_proc_core/core/regs/addr_out
add wave -noupdate -format Literal /tb_proc_core/core/regs/d_reg
add wave -noupdate -format Literal /tb_proc_core/core/regs/a_reg
add wave -noupdate -format Literal /tb_proc_core/core/regs/x_reg
add wave -noupdate -format Literal /tb_proc_core/core/regs/y_reg
add wave -noupdate -format Literal /tb_proc_core/core/regs/s_reg
add wave -noupdate -format Literal /tb_proc_core/core/regs/p_reg
add wave -noupdate -format Literal /tb_proc_core/core/regs/pc_out
add wave -noupdate -format Literal /tb_proc_core/core/regs/dreg
add wave -noupdate -format Literal /tb_proc_core/core/regs/a_reg_i
add wave -noupdate -format Literal /tb_proc_core/core/regs/x_reg_i
add wave -noupdate -format Literal /tb_proc_core/core/regs/y_reg_i
add wave -noupdate -format Literal /tb_proc_core/core/regs/selected_idx
add wave -noupdate -format Literal /tb_proc_core/core/regs/i_reg_i
add wave -noupdate -format Literal /tb_proc_core/core/regs/s_reg_i
add wave -noupdate -format Literal /tb_proc_core/core/regs/p_reg_i
add wave -noupdate -format Literal /tb_proc_core/core/regs/pcl
add wave -noupdate -format Literal /tb_proc_core/core/regs/pch
add wave -noupdate -format Literal /tb_proc_core/core/regs/adl
add wave -noupdate -format Literal /tb_proc_core/core/regs/adh
add wave -noupdate -format Logic /tb_proc_core/core/regs/pc_carry_i
add wave -noupdate -format Logic /tb_proc_core/core/regs/pc_carry_d
add wave -noupdate -format Logic /tb_proc_core/core/regs/branch_flag
add wave -noupdate -format Literal /tb_proc_core/core/regs/reg_out
add wave -noupdate -format Literal /tb_proc_core/core/regs/vect
add wave -noupdate -format Logic /tb_proc_core/core/regs/dreg_zero
add wave -noupdate -format Logic /tb_proc_core/core/regs/n_flag
add wave -noupdate -format Logic /tb_proc_core/core/regs/v_flag
add wave -noupdate -format Logic /tb_proc_core/core/regs/b_flag
add wave -noupdate -format Logic /tb_proc_core/core/regs/d_flag
add wave -noupdate -format Logic /tb_proc_core/core/regs/i_flag
add wave -noupdate -format Logic /tb_proc_core/core/regs/z_flag
add wave -noupdate -format Logic /tb_proc_core/core/regs/c_flag
add wave -noupdate -divider *
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
add wave -noupdate -format Logic /tb_proc_core/core/oper/shift_en
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
add wave -noupdate -divider alu
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
TreeUpdate [SetDefaultTree]
WaveRestoreCursors {{Cursor 1} {1557522 ps} 0}
configure wave -namecolwidth 198
configure wave -valuecolwidth 52
configure wave -justifyvalue left
configure wave -signalnamewidth 2
configure wave -snapdistance 10
configure wave -datasetprefix 0
configure wave -rowmargin 4
configure wave -childrowmargin 2
configure wave -gridoffset 0
configure wave -gridperiod 1
configure wave -griddelta 40
configure wave -timeline 0
update
WaveRestoreZoom {0 ps} {6843208 ps}
