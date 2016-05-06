-------------------------------------------------------------------------------
-- Title      : CRC32 calculator
-------------------------------------------------------------------------------
-- File       : crc32.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module calculates a 32-bit CRC over the incoming data-
--              stream (N-bits at a time). Note that the last bit of the
--              polynom needs to be set to '0', due to the way the code
--              is constructed.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity crc32 is
generic (
    g_data_width    : natural := 8 );
port (
    clock       : in  std_logic;
    clock_en    : in  std_logic;
    sync        : in  std_logic;
    data        : in  std_logic_vector(g_data_width-1 downto 0);
    data_valid  : in  std_logic;
    
    crc         : out std_logic_vector(31 downto 0) );
end crc32;

architecture behavioral of crc32 is
    signal crc_reg   : std_logic_vector(31 downto 0) := (others => '0');
    constant polynom : std_logic_vector(31 downto 0) := X"04C11DB6";

-- CRC-32 = x32 + x26 + x23 + x22 + x16 + x12 + x11 + x10 + x8 + x7 + x5 + x4 + x2 + x + 1 (used in Ethernet) 
-- 3322 2222 2222 1111 1111 1100 0000 0000
-- 1098 7654 3210 9876 5432 1098 7654 3210
-- 0000 0100 1100 0001 0001 1101 1011 0111    

begin
    process(clock)
        function new_crc(i, p: std_logic_vector; data : std_logic) return std_logic_vector is
            variable sh : std_logic_vector(i'range);
            variable d  : std_logic;
        begin
            d := data xor i(i'high);
            sh := i(i'high-1 downto 0) & d; --'0';
            if d = '1' then
                sh := sh xor p;
            end if;
            return sh;
        end new_crc;

        variable tmp : std_logic_vector(crc_reg'range);
    begin
        if rising_edge(clock) then
            if clock_en='1' then
                if data_valid='1' then
                    if sync='1' then
                        tmp := (others => '1');
                    else
                        tmp := crc_reg;
                    end if;
                    
                    for i in data'reverse_range loop -- LSB first!
                        tmp := new_crc(tmp, polynom, data(i));
                    end loop;
                    crc_reg <= tmp;
                elsif sync='1' then
                    crc_reg <= (others => '1');
                end if;
            end if;
        end if;
    end process;   

    process(crc_reg)
    begin
        for i in 0 to 31 loop
            crc(i) <= not crc_reg(31-i);
        end loop;
    end process;
end behavioral;
