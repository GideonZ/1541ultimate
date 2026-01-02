-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Character Generator
-------------------------------------------------------------------------------
-- File       : char_generator_rom.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Character generator ROM
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.char_generator_rom_pkg.all;

entity char_generator_rom is
port (
    clock       : in  std_logic;
    enable      : in  std_logic := '1';
    address     : in  unsigned(10 downto 0);
    data        : out std_logic_vector(7 downto 0) );
end entity;

architecture rom of char_generator_rom is
    signal data_i   : std_logic_vector(7 downto 0) := X"00";
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if enable='1' then
                data_i <= char_generator_rom_array(to_integer(address));
            end if;
        end if;
    end process;
    data <= data_i;
end rom;
