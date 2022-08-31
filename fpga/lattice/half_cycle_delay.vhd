-------------------------------------------------------------------------------
-- Title      : Half Cycle Delay for DDR output signals
-------------------------------------------------------------------------------
-- Description: This module implements a simple delay of half a cycle for
--              signals that are encoded for DDR transmission. It cycles
--              the bits half a word, eg: 3210 => 10xx, xx32.
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    
entity half_cycle_delay is
generic (
    g_bits      : natural := 8 );
port (
    clock       : in  std_logic := '0';
    din         : in  std_logic_vector(g_bits-1 downto 0);
    dout        : out std_logic_vector(g_bits-1 downto 0) );
end entity;    

architecture Gideon of half_cycle_delay is
    constant c_half     : natural := g_bits/2;
    signal remainder    : std_logic_vector(c_half-1 downto 0) := (others => '0');
begin
    remainder <= din(din'high downto c_half) when rising_edge(clock);
    dout <= din(c_half-1 downto 0) & remainder;
end architecture;
