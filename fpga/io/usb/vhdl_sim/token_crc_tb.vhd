-------------------------------------------------------------------------------
-- Title      : token_crc.vhd
-------------------------------------------------------------------------------
-- File       : token_crc.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This file is used to calculate the CRC over a USB token
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity token_crc_tb is

end token_crc_tb;

architecture tb of token_crc_tb is
    signal clock    : std_logic := '0';
    signal token_in : std_logic_vector(10 downto 0);
    signal crc      : std_logic_vector(4 downto 0);
    signal total    : std_logic_vector(15 downto 0);
begin

    i_mut: entity work.token_crc
    port map (
        clock       => clock,
        sync        => '1',
        token_in    => token_in,
        
        crc         => crc );

    clock <= not clock after 10 ns;

    p_test: process
    begin
        token_in <= "0001" & "0000001"; -- EP=1 / ADDR=1
        wait until clock='1';
        wait until clock='1';
        wait until clock='1';
        token_in <= "111" & X"FB";
        wait until clock='1';
        wait until clock='1';
        wait until clock='1';
        token_in <= "000" & X"01";

        wait;
    end process;    

    total <= crc & token_in;

end tb;
