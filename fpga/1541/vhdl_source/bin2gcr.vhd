
library ieee;
use ieee.std_logic_1164.all;

entity bin2gcr is
port (
    d_in        : in  std_logic_vector(3 downto 0);
    d_out       : out std_logic_vector(4 downto 0) );
end bin2gcr;

architecture rom of bin2gcr is
begin
    with d_in select d_out <=
       "01010" when "0000",
       "01011" when "0001",
       "10010" when "0010",
       "10011" when "0011",
       "01110" when "0100",
       "01111" when "0101",
       "10110" when "0110",
       "10111" when "0111",
       "01001" when "1000",
       "11001" when "1001",
       "11010" when "1010",
       "11011" when "1011",
       "01101" when "1100",
       "11101" when "1101",
       "11110" when "1110",
       "10101" when "1111",
       "11111" when others; -- does not occur
        
end rom;
