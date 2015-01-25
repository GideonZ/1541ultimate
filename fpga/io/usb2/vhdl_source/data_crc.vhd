-------------------------------------------------------------------------------
-- Title      : data_crc.vhd
-------------------------------------------------------------------------------
-- File       : data_crc.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This file is used to calculate the CRC over a USB data
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity data_crc is
port (
    clock       : in  std_logic;
    sync        : in  std_logic;
    valid       : in  std_logic;
    data_in     : in  std_logic_vector(7 downto 0);
    
    crc         : out std_logic_vector(15 downto 0) );
end data_crc;

architecture Gideon of data_crc is
    constant polynom : std_logic_vector(15 downto 0) := X"8004";
-- CRC-5 = x5 + x2 + 1

begin
    process(clock)
        variable tmp : std_logic_vector(crc'range);
        variable d  : std_logic;
    begin
        if rising_edge(clock) then
            if sync = '1' then
                tmp := (others => '1');
            end if;
            if valid = '1' then
                for i in data_in'reverse_range loop -- LSB first!
                    d := data_in(i) xor tmp(tmp'high);
                    tmp := tmp(tmp'high-1 downto 0) & d; --'0';
                    if d = '1' then
                        tmp := tmp xor polynom;
                    end if;                                
                end loop;
            end if;
            for i in tmp'range loop -- reverse and invert
                crc(crc'high-i) <= not(tmp(i));
            end loop;
        end if;
    end process;   
end Gideon;
