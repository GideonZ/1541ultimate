library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;

entity sine_osc is
port (
    clock   : in  std_logic;
    enable  : in  std_logic := '1';
    reset   : in  std_logic;
    
    sine    : out signed(15 downto 0);
    cosine  : out signed(15 downto 0) );
end sine_osc;

architecture gideon of sine_osc is
    signal cos_i    : signed(15 downto 0);
    signal sin_i    : signed(15 downto 0);
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if reset='1' then
                sin_i <= X"0000";
                cos_i <= X"7FFF";
            elsif enable='1' then
                sin_i <= sum_limit(shift_right(cos_i, 8), sin_i);
                cos_i <= sub_limit(cos_i, shift_right(sin_i, 8));
            end if;
        end if;
    end process;
    
    sine <= sin_i;
    cosine <= cos_i;
    
end gideon;
