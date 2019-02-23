library ieee;
use ieee.std_logic_1164.all;

library mblite;
use mblite.config_Pkg.all;
use mblite.core_Pkg.all;
use mblite.std_Pkg.all;

library work;

entity cached_mblite is
generic (
    g_icache        : boolean := true;
    g_dcache        : boolean := true );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    disable_i   : in  std_logic := '0';
    disable_d   : in  std_logic := '0';
    
    invalidate  : in  std_logic := '0';
    inv_addr    : in  std_logic_vector(31 downto 0);
    
    dmem_o      : out dmem_out_type;
    dmem_i      : in  dmem_in_type;

    imem_o      : out dmem_out_type;
    imem_i      : in  dmem_in_type;

    irq_i       : in  std_logic;
    irq_o       : out std_logic );

end entity;

architecture structural of cached_mblite is

    -- signals from processor to cache
    signal cimem_o : imem_out_type;
    signal cimem_i : imem_in_type;
    signal cdmem_o : dmem_out_type;
    signal cdmem_i : dmem_in_type;

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

    r_icache: if g_icache generate
        i_cache: entity work.dm_simple
        generic map (
            g_data_register => true,
            g_mem_direct    => true )
        port map (
            clock  => clock,
            reset  => reset,
    
            disable => disable_i,
            
            dmem_i.adr_o => cimem_o.adr_o,
            dmem_i.ena_o => cimem_o.ena_o,
            dmem_i.sel_o => "0000",
            dmem_i.we_o  => '0',
            dmem_i.dat_o => (others => '0'),
            
            dmem_o.ena_i => cimem_i.ena_i,
            dmem_o.dat_i => cimem_i.dat_i,
    
            mem_o  => imem_o,
            mem_i  => imem_i );
        end generate;
      
    r_no_icache: if not g_icache generate
        imem_o.adr_o <= cimem_o.adr_o;
        imem_o.ena_o <= cimem_o.ena_o;
        imem_o.sel_o <= X"F";
        imem_o.we_o  <= '0';
        imem_o.dat_o <= (others => '0');
        
        cimem_i.dat_i <= imem_i.dat_i;
        cimem_i.ena_i <= imem_i.ena_i;
    end generate;


    r_dcache: if g_dcache generate
        d_cache: entity work.dm_simple --with_invalidate
        generic map (
            g_data_register => true,
            g_mem_direct    => true )
        port map (
            clock      => clock,
            reset      => reset,
    
            disable    => disable_d,
    --        invalidate => invalidate,
    --        inv_addr   => inv_addr,
    
            dmem_i     => cdmem_o,
            dmem_o     => cdmem_i,
                       
            mem_o      => dmem_o,
            mem_i      => dmem_i );
    end generate;
    
    r_no_dcache: if not g_dcache generate
        -- direct connection to outside world
        dmem_o <= cdmem_o;
        cdmem_i <= dmem_i;
    end generate;
    
end architecture;
