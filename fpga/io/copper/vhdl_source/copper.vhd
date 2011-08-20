-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2011 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- 
-- Author: Gideon Zweijtzer (gideon.zweijtzer (at) gmail.com)
--
-- Note that this file is copyrighted, and is not supposed to be used in other
-- projects without written permission from the author.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.slot_bus_pkg.all;
use work.dma_bus_pkg.all;

use work.copper_pkg.all;

entity copper is
generic (
    g_copper_size : natural := 12 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    irq_n       : in  std_logic;
    phi2_tick   : in  std_logic;
    
    trigger_1   : out std_logic;
    trigger_2   : out std_logic;

    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
    
    dma_req     : out t_dma_req;
    dma_resp    : in  t_dma_resp;
    
    slot_req    : in  t_slot_req;
    slot_resp   : out t_slot_resp );
end entity;

architecture gideon of copper is
    signal io_req_regs  : t_io_req;
    signal io_resp_regs : t_io_resp;

    signal io_req_ram   : t_io_req;
    signal io_resp_ram  : t_io_resp;

    signal ram_rdata    : std_logic_vector(7 downto 0);
    signal ram_gate     : std_logic := '0';
    
    signal copper_addr  : unsigned(g_copper_size-1 downto 0);
    signal copper_rdata : std_logic_vector(7 downto 0);
    signal copper_wdata : std_logic_vector(7 downto 0);
    signal copper_we    : std_logic;
    signal copper_en    : std_logic;
    
    signal control      : t_copper_control;
    signal status       : t_copper_status;
begin
    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 12,
        g_range_hi  => 12,
        g_ports     => 2 )
    port map (
        clock    => clock,
        
        req      => io_req,
        resp     => io_resp,
        
        reqs(0)  => io_req_regs,
        reqs(1)  => io_req_ram,
        
        resps(0) => io_resp_regs,
        resps(1) => io_resp_ram );

    i_regs: entity work.copper_regs
    port map (
        clock    => clock,
        reset    => reset,
        
        io_req   => io_req_regs,
        io_resp  => io_resp_regs,
        
        control  => control,
        status   => status );
        
    i_ram: entity work.dpram
    generic map (
        g_width_bits    => 8,
        g_depth_bits    => g_copper_size,
        g_read_first_a  => false,
        g_read_first_b  => false,
        g_storage       => "block" )
    
    port map (
        a_clock         => clock,
        a_address       => io_req_ram.address(g_copper_size-1 downto 0),
        a_rdata         => ram_rdata,
        a_wdata         => io_req_ram.data,
        a_en            => '1',
        a_we            => io_req_ram.write,

        b_clock         => clock,
        b_address       => copper_addr,
        b_rdata         => copper_rdata,
        b_wdata         => copper_wdata,
        b_en            => copper_en,
        b_we            => copper_we );
        
    process(clock)
    begin
        if rising_edge(clock) then
            io_resp_ram.ack <= io_req_ram.read or io_req_ram.write;
            ram_gate        <= io_req_ram.read;
        end if;
    end process;
    io_resp_ram.data <= ram_rdata when ram_gate='1' else X"00";

    i_fsm: entity work.copper_engine
    generic map (
        g_copper_size => g_copper_size )
    port map (
        clock       => clock,
        reset       => reset,
        
        irq_n       => irq_n,
        phi2_tick   => phi2_tick,

        ram_address => copper_addr,
        ram_rdata   => copper_rdata,
        ram_wdata   => copper_wdata,
        ram_en      => copper_en,
        ram_we      => copper_we,
        
        trigger_1   => trigger_1,
        trigger_2   => trigger_2,

        slot_req    => slot_req,
        slot_resp   => slot_resp,
        
        dma_req     => dma_req,
        dma_resp    => dma_resp,

        control     => control,
        status      => status );

end gideon;
