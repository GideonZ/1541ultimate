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
    signal sid_write    : std_logic;
begin
    i_sid_engine: entity work.sid_top
    generic map (
        g_filter_div  => g_filter_div, -- for 50 MHz
        g_num_voices  => g_num_voices )
    port map (
        clock         => clock,
        reset         => reset,
                      
        addr          => slot_req.bus_address(7 downto 0),
        wren          => sid_write,
        wdata         => slot_req.data,
        rdata         => slot_resp.data,

        io_req_filt   => io_req,
        io_resp_filt  => io_resp,
    
        start_iter    => start_iter,
        sample_left   => sample_left,
        sample_right  => sample_right );
    
    sid_write <= '1' when slot_req.bus_write='1' and slot_req.bus_address(15 downto 8)=X"D4" else '0';
    
end structural;
