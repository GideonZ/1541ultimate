-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Character Generator
-------------------------------------------------------------------------------
-- File       : char_generator.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Character generator top
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity char_generator_tb is

end;

architecture tb of char_generator_tb is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal io_req          : t_io_req := c_io_req_init;
    signal io_resp         : t_io_resp;
    signal h_sync          : std_logic := '0';
    signal v_sync          : std_logic := '0';
    signal pixel_active    : std_logic;
    signal pixel_data      : std_logic;
begin
    clock <= not clock after 35714 ps;
    reset <= '1', '0' after 100 ns;
    
    i_char_gen: entity work.char_generator
    port map (
        clock           => clock,
        reset           => reset,
            
        io_req          => io_req,
        io_resp         => io_resp,
    
        h_sync          => h_sync,
        v_sync          => v_sync,
        
        pixel_active    => pixel_active,
        pixel_data      => pixel_data );

end tb;
