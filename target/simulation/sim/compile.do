
vlib mblite

vcom -work mblite ../../../fpga/cpu_unit/vhdl_source/config_pkg.vhd

vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/std/std_Pkg.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/std/dsram.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/std/sram.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/std/sram_4en.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/core_Pkg.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/decode.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/execute.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/fetch.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/gprf.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/mem.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/core.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/core_address_decoder.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/core_wb_adapter.vhd
vcom -work mblite ../../../fpga/cpu_unit/mblite/hw/core/core_wb.vhd

vcom -work mblite ../../../fpga/ip/memory/vhdl_source/dpram_sc.vhd
vcom -work mblite ../../../fpga/cpu_unit/vhdl_source/dm_simple.vhd
vcom -work mblite ../../../fpga/cpu_unit/vhdl_source/dm_with_invalidate.vhd
vcom -work mblite ../../../fpga/cpu_unit/vhdl_source/dmem_arbiter.vhd
vcom -work mblite ../../../fpga/cpu_unit/vhdl_source/cached_mblite.vhd

vlib unisim

vcom -work unisim ../../../fpga/altera/xilinx_primitives/unisim_VPKG.vhd
vcom -work unisim ../../../fpga/altera/xilinx_primitives/unisim_VCOMP.vhd
vcom -work unisim ../../../fpga/altera/xilinx_primitives/RAMB16_S9_S18.vhd
vcom -work unisim ../../../fpga/altera/xilinx_primitives/RAMB16_S9_S36.vhd
vcom -work unisim ../../../fpga/altera/xilinx_primitives/RAMB16BWE_S36_S9.vhd
