--------------------------------------------------------------------------------
-- Entity: mul_add
-- Date:2018-08-02  
-- Author: gideon     
--
-- Description: VHDL only version of multiply accumulate with double accu
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mul_add is
	port  (
        clock       : in  std_logic;
        clear       : in  std_logic;

        a           : in  signed(17 downto 0);
        b           : in  signed(8 downto 0);
        
        result      : out signed(31 downto 0)
	);
end entity;

architecture arch of mul_add is
    signal mult         : signed(26 downto 0);
    signal accu_reg1    : signed(31 downto 0);
    signal accu_reg2    : signed(31 downto 0);
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if clear = '1' then
                accu_reg1 <= (others => '0');
                accu_reg2 <= (others => '0');
            else
                accu_reg1 <= accu_reg2 + mult;
                accu_reg2 <= accu_reg1;
            end if;
            mult <= a * b;                            
        end if;
    end process;
    result <= accu_reg1;
end architecture;

