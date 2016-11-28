-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2004, Gideon's Logic Architectures
--
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

entity token_crc is
port (
    clock       : in  std_logic;
    token_in    : in  std_logic_vector(10 downto 0);
    crc         : out std_logic_vector(4 downto 0) );
end token_crc;

architecture Gideon of token_crc is
    -- CRC-5 = x5 + x2 + 1
    constant polynom : std_logic_vector(4 downto 0) := "00101";

begin
    process(clock)
        variable tmp : std_logic_vector(crc'range);
        variable d  : std_logic;
    begin
        if rising_edge(clock) then
            tmp := (others => '1');
            for i in token_in'reverse_range loop -- LSB first!
                d := tmp(tmp'high) xor token_in(i); -- loop xor
                tmp := tmp(tmp'high-1 downto 0) & '0'; -- cyclic shift
                if d = '1' then -- if highest bit was '1' then apply polynom
                    tmp := tmp xor polynom;
                end if;                                
                tmp(0) := d;
            end loop;
            for i in tmp'range loop -- reverse
                crc(crc'high-i) <= not tmp(i);
            end loop;
        end if;
    end process;   
end Gideon;
