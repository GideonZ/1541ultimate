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
        reset       : in  std_logic;
        
        rdata       : in  std_logic_vector(31 downto 0);
        rdata_valid : in  std_logic;
        first_word  : in  std_logic;
        last_word   : in  std_logic;
        offset      : in  unsigned(1 downto 0);
        last_bytes  : in  unsigned(1 downto 0);
        wdata       : out std_logic_vector(31 downto 0);
        wmask       : out std_logic_vector(3 downto 0);
        wnext       : out std_logic );

end align_read_to_bram;

-- This unit implements data rotation. This is done to support streaming from the avalon.
-- Note that the other unit: mem32_to_avalon bridge, actually "emulates" the DRAM wrapped
-- bursts. So.. the data that this unit gets is always lane aligned, but not word aligned.
-- To minimize the size of this block, only offsets of 0 and 2 are supported.

-- Length that this unit gets is: actual length + offset + 3. This indicates the last byte that
-- is being read and thus valid for writing. 
-- int (size / 4) = number of words to be accessed.
-- (size and 3) = info about byte enables of last beat 0 = 0001, 1 = 0011, 2 = 0111, 3 = 1111.
-- for writing, these byte enables shall still be rotated to the right. 
-- offset = info about byte enables of first beat, and rotation value
-- Note that for an offset of 0, it doesn't really matter if we write a few extra bytes in the BRAM,
-- because we're aligned. However, for an offset other than 0 (=2 in this case), it determines whether
-- we should write the last beat or not.

architecture arch of align_read_to_bram is
    type t_state is (idle, stream, last);
    signal state : t_state;
    signal remain       : std_logic_vector(31 downto 0) := (others => '0');
begin
    process(clock)
    begin
        if rising_edge(clock) then
            wmask <= X"0";
            wnext <= '0';

            case state is
            when idle =>
                wdata <= rdata;
                if rdata_valid = '1' then -- we assume first word
                    if offset(1) = '0' then -- aligned
                        wmask <= X"F";
                        wnext <= '1';
                    else -- misaligned. Data is FROM address 2; so first two bytes are invalid.
                    -- However, because rotated and little-endianed by the mem32_to_avalon block,
                    -- so those will be the leftmost bytes in little endian mode.
                    -- We get order 1032. 10 are invalid, so we store "32" (rightmost)
                        remain(15 downto 0) <= rdata(15 downto 0);
                        if last_word = '1' then
                            state <= last;
                        else
                            state <= stream;
                        end if;
                    end if;
                end if;
            
            when stream =>
                -- again, we get 5476. We stored 32. We store 76 and write 5432.
                wdata <= rdata(31 downto 16) & remain(15 downto 0);
                if rdata_valid = '1' then
                    remain(15 downto 0) <= rdata(15 downto 0);
                    wmask <= X"F";
                    wnext <= '1';
                    if last_word = '1' then
                        if last_bytes(1) = '1' then
                            state <= last;
                        else
                            state <= idle;
                        end if;
                    end if;
                end if;
            
            when last =>
                -- we stored 67, we write 67--
                wdata <= rdata(31 downto 16) & remain(15 downto 0);
                wmask <= "0011";
                state <= idle;
                
            when others =>
                null;
            end case;
            
            if reset = '1' then
                state <= idle;
            end if;
        end if;
    end process;

end arch;
