# cd f:/proj/commodore/8bit_cpu/new_6502/sim

vcom -93 ../../../target/simulation/packages/vhdl_source/tl_string_util_pkg.vhd
#vcom -93 ../../../target/simulation/packages/vhdl_source/file_io_pkg.vhd
vcom -93 ../../../target/simulation/packages/vhdl_source/tl_file_io_pkg.vhd
vcom -93 ../../../target/simulation/packages/vhdl_source/tl_flat_memory_model_pkg.vhd
vcom -93 ../vhdl_source/pkg_6502_defs.vhd
vcom -93 ../vhdl_source/pkg_6502_decode.vhd
vcom -93 ../vhdl_source/pkg_6502_opcodes.vhd
vcom -93 ../vhdl_source/proc_control.vhd
vcom -93 ../vhdl_sim/tb_proc_control.vhd
#vcom -93 ../vhdl_sim/tb_decode.vhd
vcom -93 ../vhdl_source/shifter.vhd
vcom -93 ../vhdl_source/alu.vhd
#vcom -93 ../vhdl_sim/tb_alu.vhd
vcom -93 ../vhdl_source/implied.vhd
vcom -93 ../vhdl_source/bit_cpx_cpy.vhd
vcom -93 ../vhdl_source/data_oper.vhd
vcom -93 ../vhdl_sim/tb_data_oper.vhd
vcom -93 ../vhdl_source/proc_registers.vhd
vcom -93 ../vhdl_source/proc_interrupt.vhd
vcom -93 ../vhdl_source/proc_core.vhd

vcom -93 ../vhdl_sim/tb_proc_core.vhd
