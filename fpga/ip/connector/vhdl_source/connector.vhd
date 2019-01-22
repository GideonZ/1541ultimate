--------------------------------------------------------------------------------
-- Entity: connector
-- Date:2018-08-05  
-- Author: gideon     
--
-- Description: This simple module connects two tristate "TTL" buffers
--              This module does not allow external drivers to the net.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity connector is
generic (
    g_usedbus : boolean := true;
    g_width : natural := 8 );
port (
    a_o     : in  std_logic_vector(g_width-1 downto 0);
    a_t     : in  std_logic_vector(g_width-1 downto 0);
    a_i     : out std_logic_vector(g_width-1 downto 0);
    dbus    : in  std_logic_vector(g_width-1 downto 0) := (others => '1');
    
    b_o     : in  std_logic_vector(g_width-1 downto 0);
    b_t     : in  std_logic_vector(g_width-1 downto 0);
    b_i     : out std_logic_vector(g_width-1 downto 0);
    
    connect : in  std_logic );

end entity;

architecture arch of connector is
begin
    process(a_o, a_t, b_o, b_t, connect, dbus)
    begin
        if connect = '0' then
            if g_usedbus then
                a_i <= dbus;
            else
                a_i <= a_o or not a_t;
            end if;
            b_i <= b_o or not b_t;
        
        else -- connected
            -- '0' when a_o = '0' and a_t = '1', or b_o = '0' and b_t = '1'
            a_i <= not ((not a_o and a_t) or (not b_o and b_t));
            b_i <= not ((not a_o and a_t) or (not b_o and b_t));
        end if;
    end process;

end architecture;
