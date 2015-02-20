--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: timer
-- Date:2015-02-19  
-- Author: Gideon     
-- Description: Generic timeout timer
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity timer is
    generic (
        g_width     : natural := 10 );
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;

        start       : in  std_logic;
        start_value : in  unsigned(g_width-1 downto 0);
        
        timeout     : out std_logic ); 
        
end entity;

architecture arch of timer is
    signal running      : std_logic;
    signal count        : unsigned(g_width-1 downto 0);
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if start = '1' then
                count <= start_value;
                running <= '1';
                timeout <= '0';
            elsif running = '1' then
                count <= count - 1;
            end if;

            if count = 1 then
                timeout <= '1';
                running <= '0';
            end if;

            if reset='1' then
                count <= (others => '0');
                running <= '0';
                timeout <= '0'; 
            end if;
        end if;
    end process;
end arch;
