
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pulse_stretch is
generic (
	g_duration	: natural := 200 );
port (
	clock		: in  std_logic;
	reset		: in  std_logic;
	
	pulse_in	: in  std_logic;
	pulse_out	: out std_logic );
end;

architecture rtl of pulse_stretch is
	signal count : natural range 0 to g_duration-1;
begin
	process(clock)
	begin
		if rising_edge(clock) then
			if reset='1' then
				pulse_out <= '0';
				count <= 0;
			elsif pulse_in='1' then
				pulse_out <= '1';
				count <= g_duration-1;
			elsif count = 0 then
				pulse_out <= '0';
			else
				count <= count - 1;
			end if;
		end if;
	end process;
end rtl;
