-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 Gideon's Logic Architectures'
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
use work.sid_io_regs_pkg.all;

entity sid_peripheral is
generic (
    g_filter_div  : natural := 221; -- for 50 MHz
    g_num_voices  : natural := 16 );
port (
    clock         : in  std_logic;
    reset         : in  std_logic;
                  
    slot_req      : in  t_slot_req;
    slot_resp     : out t_slot_resp;
    
    io_req        : in  t_io_req;
    io_resp       : out t_io_resp;
                  
    start_iter    : in  std_logic;
    sample_left   : out signed(17 downto 0);
    sample_right  : out signed(17 downto 0) );

end sid_peripheral;

architecture structural of sid_peripheral is
    signal io_req_regs  : t_io_req;
    signal io_resp_regs : t_io_resp;

    signal io_req_filt  : t_io_req;
    signal io_resp_filt : t_io_resp;

    signal control      : t_sid_control;

    signal sid_addr     : unsigned(7 downto 0);
    signal sid_wren     : std_logic;
    signal sid_wdata    : std_logic_vector(7 downto 0);
    signal sid_rdata    : std_logic_vector(7 downto 0);

    signal sid_write    : std_logic;
begin
    -- first we split our I/O bus in max 4 ranges, of 2K each.
    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 11,
        g_range_hi  => 12,
        g_ports     => 2 )
    port map (
        clock    => clock,
        
        req      => io_req,
        resp     => io_resp,
        
        reqs(0)  => io_req_regs, -- 4042000
        reqs(1)  => io_req_filt, -- 4042800
        
        resps(0) => io_resp_regs,
        resps(1) => io_resp_filt );

    i_regs: entity work.sid_io_regs
    generic map (
        g_filter_div  => g_filter_div,
        g_num_voices  => g_num_voices )
    port map (
        clock    => clock,
        reset    => reset,
        
        io_req   => io_req_regs,
        io_resp  => io_resp_regs,
        
        control  => control );    

    i_sid_mapper: entity work.sid_mapper
    port map (
        clock         => clock,
        reset         => reset,
        
        control       => control,
        
        slot_req      => slot_req,
        slot_resp     => slot_resp,
        
        sid_addr      => sid_addr,
        sid_wren      => sid_wren,
        sid_wdata     => sid_wdata,
        sid_rdata     => sid_rdata );

    i_sid_engine: entity work.sid_top
    generic map (
        g_filter_div  => g_filter_div,
        g_num_voices  => g_num_voices )
    port map (
        clock         => clock,
        reset         => reset,
                      
        addr          => sid_addr,
        wren          => sid_wren,
        wdata         => sid_wdata,
        rdata         => sid_rdata,

        comb_wave_l   => control.comb_wave_left,
        comb_wave_r   => control.comb_wave_right,

        io_req_filt   => io_req_filt,
        io_resp_filt  => io_resp_filt,
    
        start_iter    => start_iter,
        sample_left   => sample_left,
        sample_right  => sample_right );
    
    sid_write <= '1' when slot_req.bus_write='1' and slot_req.bus_address(15 downto 8)=X"D4" else '0';
    
end structural;
