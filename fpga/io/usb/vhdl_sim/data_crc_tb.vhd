-------------------------------------------------------------------------------
-- Title      : data_crc.vhd
-------------------------------------------------------------------------------
-- File       : data_crc.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This file is used to calculate the CRC over a USB token
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity data_crc_tb is

end data_crc_tb;

architecture tb of data_crc_tb is
    signal clock    : std_logic := '0';
    signal data_in  : std_logic_vector(7 downto 0);
    signal crc      : std_logic_vector(15 downto 0);
    signal valid    : std_logic := '0';
    signal sync     : std_logic := '0';
    
    type t_byte_array is array (natural range <>) of std_logic_vector(7 downto 0);
    constant test_array : t_byte_array := (
        X"80", X"06", X"00", X"01", X"00", X"00", X"40", X"00" );
begin

    i_mut: entity work.data_crc
    port map (
        clock       => clock,
        sync        => sync,
        valid       => valid,
        data_in     => data_in,
        
        crc         => crc );

    clock <= not clock after 10 ns;

    p_test: process
    begin
        wait until clock='1';
        wait until clock='1';
        sync <= '1';
        wait until clock='1';
        sync <= '0';
        
        for i in test_array'range loop
            data_in <= test_array(i);
            valid   <= '1';
            wait until clock = '1';
        end loop;
        valid <= '0';                    
        wait;
    end process;    

end tb;
