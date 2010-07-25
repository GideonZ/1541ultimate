onerror {resume}
quietly WaveActivateNextPane {} 0
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/iec_clock
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/iec_data
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/iec_atn
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/bound
add wave -noupdate -divider {New Divider}
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/atn_o
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/clk_o
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/data_o
add wave -noupdate -divider {New Divider}
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/clk_i
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/data_i
add wave -noupdate -format Logic /tb_cpu_part_1541/iec_bfm/atn_i
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/clock
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/clock_en
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/reset
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/atn_i
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/clk_i
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/data_i
add wave -noupdate -divider {Drive Output}
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/atn_o
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/clk_o
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/data_o
add wave -noupdate -divider {New Divider}
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/drive_select
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/motor_on
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/mode
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/write_prot_n
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/step
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/soe
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/rate_ctrl
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/byte_ready
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/sync
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/drv_rdata
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/drv_wdata
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/act_led
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/cpu_write
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/cpu_wdata
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/cpu_rdata
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/cpu_addr
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/cpu_irqn
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/rom_data
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/ram_data
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via1_data
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via2_data
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/ram_en
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via1_wen
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via1_ren
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_wen
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_ren
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via1_port_a_o
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via1_port_a_t
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via1_port_a_i
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via1_port_b_o
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via1_port_b_t
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via1_port_b_i
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via1_ca1
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via1_ca2
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via1_cb1
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via1_cb2
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via1_irq
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via2_port_b_o
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via2_port_b_t
add wave -noupdate -format Literal /tb_cpu_part_1541/mut/via2_port_b_i
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_ca2_o
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_ca2_i
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_ca2_t
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_cb1_o
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_cb1_i
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_cb1_t
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_cb2_o
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_cb2_i
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_cb2_t
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/via2_irq
add wave -noupdate -format Logic /tb_cpu_part_1541/mut/cpu_led
TreeUpdate [SetDefaultTree]
WaveRestoreCursors {{Cursor 1} {1250763125000 ps} 0} {{Cursor 2} {1249999386460 ps} 0}
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
WaveRestoreZoom {1250540323550 ps} {1250799649197 ps}
