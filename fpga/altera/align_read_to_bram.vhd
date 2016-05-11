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
        wdata       : out std_logic_vector(31 downto 0);
        wmask       : out std_logic_vector(3 downto 0);
        wnext       : out std_logic );

end align_read_to_bram;

-- This unit implements data rotation. This is done to support streaming from the avalon.
-- Note that the other unit: mem32_to_avalon bridge, actually "emulates" the DRAM wrapped
-- bursts. So.. the data that this unit gets is always lane aligned, but not word aligned.
-- To minimize the size of this block, only offsets of 0 and 2 are supported.

architecture arch of align_read_to_bram is
    type t_state is (idle, stream, last);
    signal state : t_state;
    signal remain       : std_logic_vector(31 downto 0) := (others => '0');
begin
    process(clock)
    begin
        if rising_edge(clock) then
            wmask <= X"0";

            case state is
            when idle =>
                wdata <= rdata;
                if rdata_valid = '1' then -- we assume first word
                    if offset(1) = '0' then -- aligned
                        wmask <= X"F";
                    else -- misaligned. Data is FROM address 2; so first two bytes are invalid.
                    -- However, because rotated by the mem32_to_avalon block, those will be
                    -- the rightmost bytes in big endian mode. We get order 2301. 01 are invalid,
                    -- So we store "23" (leftmost)
                        remain(31 downto 16) <= rdata(31 downto 16);
                        if last_word = '1' then
                            state <= last;
                        else
                            state <= stream;
                        end if;
                    end if;
                end if;
            
            when stream =>
                -- again, we get 6745. We stored 23. We store 67 and write 2345.
                wdata <= remain(31 downto 16) & rdata(15 downto 0);
                if rdata_valid = '1' then
                    remain(31 downto 16) <= rdata(31 downto 16);
                    wmask <= X"F";
                    if last_word = '1' then
                        state <= last;
                    end if;
                end if;
            
            when last =>
                -- we stored 67, we write 67--
                wdata <= remain(31 downto 16) & rdata(15 downto 0);
                wmask <= X"1100";                  
                state <= idle;
                
            when others =>
                null;
            end case;
            
            if reset = '1' then
                state <= idle;
            end if;
        end if;
    end process;

    process(state, rdata_valid, offset)
    begin
        wnext <= '0';
        case state is
        when idle =>
            wnext <= rdata_valid and not offset(1);
        when stream =>
            wnext <= rdata_valid;
        when others =>
            null;
        end case;
    end process;
    
end arch;
