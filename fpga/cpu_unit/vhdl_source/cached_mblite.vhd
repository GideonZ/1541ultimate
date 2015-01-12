library ieee;
use ieee.std_logic_1164.all;

library mblite;
use mblite.config_Pkg.all;
use mblite.core_Pkg.all;
use mblite.std_Pkg.all;

library work;

entity cached_mblite is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    mmem_o      : out dmem_out_type;
    mmem_i      : in  dmem_in_type;

    irq_i       : in  std_logic;
    irq_o       : out std_logic );

end entity;

architecture structural of cached_mblite is

    -- signals from processor to cache
    signal cimem_o : imem_out_type;
    signal cimem_i : imem_in_type;
    signal cdmem_o : dmem_out_type;
    signal cdmem_i : dmem_in_type;

    -- signals from cache to memory
    signal imem_o : dmem_out_type;
    signal imem_i : dmem_in_type;
    signal dmem_o : dmem_out_type;
    signal dmem_i : dmem_in_type;

BEGIN
    core0 : core
    port map (
        imem_o => cimem_o,
        imem_i => cimem_i,
        dmem_o => cdmem_o,
        dmem_i => cdmem_i,
        int_i  => irq_i,
        int_o  => irq_o,
        rst_i  => reset,
        clk_i  => clock );

    i_cache: entity work.dm_simple
    generic map (
        g_data_register => false,
        g_mem_direct    => true )
    port map (
        clock  => clock,
        reset  => reset,

        dmem_i.adr_o => cimem_o.adr_o,
        dmem_i.ena_o => cimem_o.ena_o,
        dmem_i.sel_o => "0000",
        dmem_i.we_o  => '0',
        dmem_i.dat_o => (others => '0'),
        
        dmem_o.ena_i => cimem_i.ena_i,
        dmem_o.dat_i => cimem_i.dat_i,

        mem_o  => imem_o,
        mem_i  => imem_i );
    
    d_cache: entity work.dm_simple
    generic map (
--        g_address_swap  => X"00010000",
        g_data_register  => false,
        g_mem_direct     => true,
        g_registered_out => true )
    port map (
        clock  => clock,
        reset  => reset,

        dmem_i => cdmem_o,
        dmem_o => cdmem_i,

        mem_o  => dmem_o,
        mem_i  => dmem_i );

    arb: entity work.dmem_arbiter
    port map (
        clock  => clock,
        reset  => reset,
        imem_i => imem_o,
        imem_o => imem_i,
        dmem_i => dmem_o,
        dmem_o => dmem_i,
        mmem_o => mmem_o,
        mmem_i => mmem_i  );
    
end architecture;
