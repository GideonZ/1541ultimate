--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: This wrapper module combines the processor core with instruction
--              cache, bus converters to the bus definition used in the Ultimate,
--              and adds an optional boot rom at address 0.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    use work.core_pkg.all;    

entity rvlite_wrapper is
generic (
    g_gprf_ram      : string := "auto";
    g_start_addr    : std_logic_vector(31 downto 0) := X"80000000";
    g_bootrom       : boolean := true;
    g_icache        : boolean := true;
    g_tag_i         : std_logic_vector(7 downto 0) := X"20";
    g_tag_d         : std_logic_vector(7 downto 0) := X"21" );
port (
    clock           : in    std_logic;
    reset           : in    std_logic;
    
    irq_i           : in    std_logic := '0';
    
    io_req          : out   t_io_req;
    io_resp         : in    t_io_resp;
    io_busy         : out   std_logic;
    
    mem_req         : out   t_mem_req_32;
    mem_resp        : in    t_mem_resp_32 );

end entity;

architecture arch of rvlite_wrapper is

    signal dmem_o      : t_dmem_req;
    signal dmem_i      : t_dmem_resp;
    signal imem_o      : t_dmem_req;
    signal imem_i      : t_dmem_resp;
    signal cimem_o     : t_imem_req;
    signal cimem_i     : t_imem_resp;

    signal dmem_req    : t_mem_req_32;
    signal dmem_resp   : t_mem_resp_32;
    signal imem_req    : t_mem_req_32;
    signal imem_resp   : t_mem_resp_32;

    signal ibram_req   : t_bram_req;
    signal ibram_resp  : t_bram_resp;
    signal dbram_req   : t_bram_req;
    signal dbram_resp  : t_bram_resp;
begin
    i_core: entity work.core
    generic map (
        g_gprf_ram   => g_gprf_ram,
        g_start_addr => g_start_addr )
    port map (
        clock     => clock,
        reset     => reset,
        irq       => irq_i,
        imem_req  => cimem_o,
        imem_resp => cimem_i,
        dmem_req  => dmem_o,
        dmem_resp => dmem_i
    );

    r_icache: if g_icache generate
        i_cache: entity work.icache
        port map (
            clock  => clock,
            reset  => reset,
    
            disable => '0',
            
            dmem_i  => cimem_o,
            dmem_o  => cimem_i,
    
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

    i_imem: entity work.bus_converter
    generic map (
        g_tag        => g_tag_i,
        g_bootrom    => g_bootrom,
        g_bootaddr   => g_start_addr(31 downto 16), 
        g_support_io => false )
    port map (
        clock        => clock,
        reset        => reset,
        dmem_req     => imem_o,
        dmem_resp    => imem_i,
        mem_req      => imem_req,
        mem_resp     => imem_resp,
        bram_req     => ibram_req,
        bram_resp    => ibram_resp,
        io_req       => open,
        io_resp      => c_io_resp_init );
    
    i_dmem: entity work.bus_converter
    generic map (
        g_tag        => g_tag_d,
        g_io_bit     => 28,
        g_bootrom    => g_bootrom,
        g_bootaddr   => g_start_addr(31 downto 16), 
        g_support_io => true )
    port map (
        clock        => clock,
        reset        => reset,
        dmem_req     => dmem_o,
        dmem_resp    => dmem_i,
        mem_req      => dmem_req,
        mem_resp     => dmem_resp,
        bram_req     => dbram_req,
        bram_resp    => dbram_resp,
        io_busy      => io_busy,
        io_req       => io_req,
        io_resp      => io_resp );

    i_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_registered => false,
        g_ports      => 2 )
    port map (
        clock        => clock,
        reset        => reset,
        reqs(0)      => dmem_req,
        reqs(1)      => imem_req,
        resps(0)     => dmem_resp,
        resps(1)     => imem_resp,
        req          => mem_req,
        resp         => mem_resp );
    
    r_boot: if g_bootrom generate
        i_bootrom: entity work.bootrom
        port map (
            clock    => clock,
            reset    => reset,
            ireq     => ibram_req,
            iresp    => ibram_resp,
            dreq     => dbram_req,
            dresp    => dbram_resp
        );        
    end generate;

end architecture;
