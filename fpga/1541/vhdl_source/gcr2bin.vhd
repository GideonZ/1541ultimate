library ieee;
use ieee.std_logic_1164.all;

entity gcr2bin is
port (
    d_in        : in  std_logic_vector(4 downto 0);
    d_out       : out std_logic_vector(3 downto 0);
    error       : out std_logic );
end gcr2bin;

architecture rom of gcr2bin is
begin
    process(d_in)
    begin
        d_out <= X"0";
        error <= '0';

        case d_in is
        when "01010" => d_out <= "0000";
        when "01011" => d_out <= "0001";
        when "10010" => d_out <= "0010";
        when "10011" => d_out <= "0011";
        when "01110" => d_out <= "0100";
        when "01111" => d_out <= "0101";
        when "10110" => d_out <= "0110";
        when "10111" => d_out <= "0111";
        when "01001" => d_out <= "1000";
        when "11001" => d_out <= "1001";
        when "11010" => d_out <= "1010";
        when "11011" => d_out <= "1011";
        when "01101" => d_out <= "1100";
        when "11101" => d_out <= "1101";
        when "11110" => d_out <= "1110";
        when "10101" => d_out <= "1111";
        when others =>
            error <= '1';
        end case;            
    end process;
end rom;
