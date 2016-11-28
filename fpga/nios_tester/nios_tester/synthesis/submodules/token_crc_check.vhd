-------------------------------------------------------------------------------
-- Title      : token_crc_check.vhd
-------------------------------------------------------------------------------
-- File       : token_crc_check.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This file is used to calculate the CRC over a USB data
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity token_crc_check is
port (
    clock       : in  std_logic;
    sync        : in  std_logic;
    valid       : in  std_logic;
    data_in     : in  std_logic_vector(7 downto 0);
    crc         : out std_logic_vector(4 downto 0) );
end token_crc_check;

architecture Gideon of token_crc_check is
    constant polynom : std_logic_vector(4 downto 0) := "00100";
    signal   crc_reg : std_logic_vector(polynom'range);
-- CRC-5 = x5 + x2 + 1
begin
    process(clock)
        variable tmp : std_logic_vector(crc'range);
        variable d  : std_logic;
    begin
        if rising_edge(clock) then
            if sync = '1' then
                crc_reg <= (others => '1');
            elsif valid = '1' then
                tmp := crc_reg;
                for i in data_in'reverse_range loop -- LSB first!
                    d := data_in(i) xor tmp(tmp'high);
                    tmp := tmp(tmp'high-1 downto 0) & d;
                    if d = '1' then
                        tmp := tmp xor polynom;
                    end if;                                
                end loop;
                crc_reg <= tmp;
            end if;
        end if;
    end process;   
    process(crc_reg)
    begin
        for i in crc_reg'range loop -- reverse and invert
            crc(crc'high-i) <= not(crc_reg(i));
        end loop;
    end process;
end Gideon;
