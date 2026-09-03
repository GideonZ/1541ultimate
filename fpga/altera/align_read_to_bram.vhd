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

-- This unit implements data rotation. This is done to support streaming from memory.
-- Length that this unit gets is: actual length + offset + 3. The maximum number of bytes
-- per transfer is 512; with a maximum offset of 3 this leads to a size of 518.
-- int (size / 4) = number of words to be accessed, so at most 129 word transfers.
-- Word accesses *should* be word aligned, to provide compatibility with every memory subsystem.
-- offset = info about byte enables of first beat, and rotation value

-- Note that writing a few extra bytes to the BRAM is never a problem. As said, the maximum
-- transfer is 512 bytes. With an offset of 0 this leads to (512 + 3) / 4 = 128 beats, any
-- other offset leads to 129 beats. Then, the FIRST beat will be incomplete, so the LAST beat
-- can always be written: 129 - 1 = 128, and always fits within the 512 byte block.


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

            if rdata_valid = '1' then -- we assume first word
                remain <= rdata;
            end if;

            -- Since the last two address bits are forced to zero, we always get 3210, regardless of the offset.
            -- If the offset is 0, we pass all data (3210)
            -- If the offset is 1, we save 3 bytes  (321x), and go to the next state
            -- If the offset is 2, we save 2 bytes  (32xx), and go to the next state
            -- If the offset is 3, we save 1 byte   (3xxx), and go to the next state

            case state is
            when idle =>
                if rdata_valid = '1' then -- we assume first word
                    case offset is
                    when "00" => -- aligned
                        wmask <= X"F";
                        wnext <= '1';
                    when others =>
                        if last_word = '1' then
                            state <= last;
                        else
                            state <= stream;
                        end if;
                    end case;
                end if;
            
            when stream =>
                if rdata_valid = '1' then
                    wmask <= X"F";
                    wnext <= '1';
                    -- it's possible that the last word will primarily end up in
                    -- remain, and not in wmask. However, because the number of bytes
                    -- read is always 3 more than necessary, the last word from
                    -- memory is also the last word that needs to be written to BRAM:
                    -- In other words we always write enough or too many, but never
                    -- too few bytes. Hence, we can always simply jump to idle.
                    if last_word = '1' then
                        state <= idle;
                    end if;
                end if;
            
            when last =>
                -- This state is only needed when the first word is also the last.
                -- Possibly this whole state machine can be eliminated when instead of
                -- writing rdata, remain is written for aligned reads. In that case,
                -- the write is simply the delayed version of rdata_valid. However,
                -- this also requires 8 more flops for storing the otherwise unused
                -- remain(7 downto 0). -> Which is smaller?!
                wmask <= X"F";
                wnext <= '1';
                state <= idle;

            when others =>
                null;
            end case;

            case offset is
            when "01" =>
                -- We use 3 bytes from the previous word (321x), and one from the current word (xxx4) => 4321
                wdata <= rdata(7 downto 0) & remain(31 downto 8);
            when "10" =>
                -- We use 2 bytes from the previous word (32xx), and two from the current word (xx54) => 5432
                wdata <= rdata(15 downto 0) & remain(31 downto 16);
            when "11" =>
                -- We use 1 bytes from the previous word (3xxx), and three from the current word (x654) => 6543
                wdata <= rdata(23 downto 0) & remain(31 downto 24);
            when others =>
                wdata <= rdata;
            end case;
            
            if reset = '1' then
                state <= idle;
            end if;
        end if;
    end process;

end arch;
