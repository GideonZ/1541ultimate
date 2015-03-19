--------------------------------------------------------------------------------
-- Entity: align_read_to_bram
-- Date:2015-03-14  
-- Author: Gideon     
--
-- Description: This module aligns 32 bit reads from memory to writes to BRAM
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity align_read_to_bram is
	port  (
		clock       : in  std_logic;

        rdata       : in  std_logic_vector(31 downto 0);
        rdata_valid : in  std_logic;
        first_word  : in  std_logic;
        last_word   : in  std_logic;
        offset      : in  unsigned(1 downto 0);
        wdata       : out std_logic_vector(31 downto 0);
        wmask       : out std_logic_vector(3 downto 0);
        wnext       : out std_logic );

end align_read_to_bram;

-- two possibilities.
-- 1) read from memory with lower address bits on zero. word from memory is aligned.
--    first byte in the word that needs to be written to BRAM depends on the offset,
--    so the word itself needs to be rotated, to get the right byte at the right 'lane'.
--
-- 2) read from memory with lower address set to the actual offset. Depending on the
--    burst size of the memory, the bytes in the word are actually shifted. Example:
--    Read from 0001 => 01.02.03.00    MSB first in this example.
--                      ** ** ** --    Write these bytes to BRAM address 0
--    Read from 0005 => 05.06.07.04    MSB first
--                               **    Write these bytes to BRAM address 0
--                      ** ** ** --    Write these bytes to BRAM address 1
--    Read from 0009 => 09.0A.0B.09
--                               **    Write these bytes to BRAM address 1
--                      ** ** ** --    Write these bytes to BRAM address 2
--    ...
--    Read from 00FD => FD.FE.FF.FC
--                               **    Write these bytes to BRAM address 62
--                      ** ** ** --    Write these bytes to BRAM address 63
--    Read from 0101 => 01.02.03.00
--                               **    Write these bytes to BRAM address 63
--    END.
--    So in this way we only need to generate the correct write strobes and address advance.
-- 
-- Note on count generation:
-- Bytes Offset | Words
--   1     x    |   1
--   2     0    |   1
--   2     1    |   1
--   2     2    |   1
--   2     3    |   2
--   3     0    |   1
--   3     1    |   1
--   3     2    |   2
--   3     3    |   2
--   4     0    |   1
--   4     1    |   2
--   4     2    |   2
--   4     3    |   2
-- (bytes + 3 + offset) and ~3
-- 
architecture arch of align_read_to_bram is
    signal need_second  : std_logic;
    signal second_cycle : std_logic;
    signal byte_en      : std_logic_vector(3 downto 0);
    signal advance      : std_logic;
begin
    process(offset, rdata_valid, first_word, last_word, second_cycle)
    begin
        need_second <= '0';
        advance <= '0';
        byte_en <= "0000";
        
        if rdata_valid='1' then
            case offset is
            when "00" => -- direct fit
                byte_en <= "1111";
                advance <= '1';
            when "01" =>
                if first_word='1' then
                    byte_en <= "0111";
                else
                    byte_en <= "1000";
                    advance <= '1';
                    need_second <= '1';
                end if;
            when "10" =>
                if first_word='1' then
                    byte_en <= "0011";
                else
                    byte_en <= "1100";
                    advance <= '1';
                    need_second <= '1';
                end if;
            when "11" =>
                if first_word='1' then
                    byte_en <= "0001";
                else
                    byte_en <= "1110";
                    advance <= '1';
                    need_second <= '1';
                end if;
            when others =>
                null;
            end case;
            if last_word='1' then
                need_second <= '0';
            end if;
        elsif second_cycle='1' then
            case offset is
            when "01" =>
                byte_en <= "0111";
            when "10" =>
                byte_en <= "0011";
            when "11" =>
                byte_en <= "0001";
            when others =>
                null;
            end case;
        end if; 
    end process;
    
    process(clock)
    begin
        if rising_edge(clock) then
            second_cycle <= need_second;
            if rdata_valid = '1' then
                wdata <= rdata;
            end if;
            wmask <= byte_en;
            wnext <= advance;
        end if;
    end process;

end arch;

