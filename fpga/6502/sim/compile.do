# cd f:/proj/commodore/8bit_cpu/new_6502/sim

vcom -93 ../../../simlibs/file_io_pkg/vhdl_sim/file_io_pkg.vhd
vcom -93 ../../../../VHDL_Lib/simlibs/flat_memory_model/vhdl_bfm/flat_memory_model.vhd
vcom -93 ../vhdl_source/pkg_6502_defs.vhd
vcom -93 ../vhdl_source/pkg_6502_decode.vhd
vcom -93 ../vhdl_source/pkg_6502_opcodes.vhd
vcom -93 ../vhdl_source/proc_control.vhd
vcom -93 ../vhdl_sim/tb_proc_control.vhd
vcom -93 ../vhdl_sim/tb_decode.vhd
vcom -93 ../vhdl_source/shifter.vhd
vcom -93 ../vhdl_source/alu.vhd
vcom -93 ../vhdl_sim/tb_alu.vhd
vcom -93 ../vhdl_source/implied.vhd
vcom -93 ../vhdl_source/bit_cpx_cpy.vhd
vcom -93 ../vhdl_source/data_oper.vhd
vcom -93 ../vhdl_sim/tb_data_oper.vhd
vcom -93 ../vhdl_source/proc_registers.vhd
vcom -93 ../vhdl_source/proc_interrupt.vhd
vcom -93 ../vhdl_source/proc_core.vhd

vcom -93 ../vhdl_sim/tb_proc_core.vhd
