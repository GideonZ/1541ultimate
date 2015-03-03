--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: mblite_sdram
-- Date:2015-01-02  
-- Author: Gideon     
-- Description: mblite processor with sdram interface - test module
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    
library mblite;
    use mblite.core_Pkg.all;

entity mblite_wrapper is
generic (
    g_tag_i         : std_logic_vector(7 downto 0) := X"20";
    g_tag_d         : std_logic_vector(7 downto 0) := X"21" );
port (
    clock           : in    std_logic;
    reset           : in    std_logic;
    
    irq_i           : in    std_logic := '0';
    irq_o           : out   std_logic;
    
    invalidate      : in    std_logic := '0';
    inv_addr        : in    std_logic_vector(31 downto 0);

    io_req          : out   t_io_req;
    io_resp         : in    t_io_resp;
    
    mem_req         : out   t_mem_req_32;
    mem_resp        : in    t_mem_resp_32 );

end entity;

architecture arch of mblite_wrapper is

    signal dmem_o      : dmem_out_type;
    signal dmem_i      : dmem_in_type;
    signal imem_o      : dmem_out_type;
    signal imem_i      : dmem_in_type;

    signal dmem_req    : t_mem_req_32;
    signal dmem_resp   : t_mem_resp_32;
    signal imem_req    : t_mem_req_32;
    signal imem_resp   : t_mem_resp_32;
begin
    i_proc: entity mblite.cached_mblite
    port map (
        clock       => clock,
        reset       => reset,
        invalidate  => invalidate,
        inv_addr    => inv_addr,
        dmem_o      => dmem_o,
        dmem_i      => dmem_i,
        imem_o      => imem_o,
        imem_i      => imem_i,
        irq_i       => irq_i,
        irq_o       => irq_o );

    i_imem: entity work.dmem_splitter
    generic map (
        g_tag        => g_tag_i,
        g_support_io => false )
    port map (
        clock        => clock,
        reset        => reset,
        dmem_i       => imem_i,
        dmem_o       => imem_o,
        mem_req      => imem_req,
        mem_resp     => imem_resp,
        io_req       => open,
        io_resp      => c_io_resp_init );
    
    i_dmem: entity work.dmem_splitter
    generic map (
        g_tag        => g_tag_d,
        g_support_io => true )
    port map (
        clock        => clock,
        reset        => reset,
        dmem_i       => dmem_i,
        dmem_o       => dmem_o,
        mem_req      => dmem_req,
        mem_resp     => dmem_resp,
        io_req       => io_req,
        io_resp      => io_resp );

    i_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_registered => false,
        g_ports      => 2 )
    port map (
        clock        => clock,
        reset        => reset,
        reqs(0)      => imem_req,
        reqs(1)      => dmem_req,
        resps(0)     => imem_resp,
        resps(1)     => dmem_resp,
        req          => mem_req,
        resp         => mem_resp );
    

    
end arch;
