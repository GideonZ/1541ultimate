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

-- Length that this unit gets is: actual length + offset + 3. This indicates the last byte that
-- is being read and thus valid for writing. 
-- int (size / 4) = number of words to be accessed.
-- (size and 3) = info about byte enables of last beat 0 = 0001, 1 = 0011, 2 = 0111, 3 = 1111.
-- for writing, these byte enables shall still be rotated to the right. 
-- offset = info about byte enables of first beat, and rotation value
-- Note that for an offset of 0, it doesn't really matter if we write a few extra bytes in the BRAM,
-- because we're aligned. However, for an offset other than 0, it determines whether
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

            -- we always get 3210, regardless of the offset.
            -- If the offset is 0, we pass all data
            -- If the offset is 1, we save 3 bytes (321x), and go to the next state
            -- If the offset is 2, we save 2 bytes (32xx), and go to the next state
            -- If the offset is 3, we save 1 byte  (3xxx), and go to the next state

            -- In case the offset was sent to the DRAM, we get:
            -- If the offset is 1, we save 3 bytes (x321), and go to the next state
            -- If the offset is 2, we save 2 bytes (xx32), and go to the next state
            -- If the offset is 3, we save 1 byte  (xxx3), and go to the next state
            case state is
            when idle =>
                wdata <= rdata;
                if rdata_valid = '1' then -- we assume first word
                    remain <= rdata;
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
                case offset is
                when "01" =>
                    -- We use 3 bytes from the previous word, and one from the current word
                    wdata <= rdata(31 downto 24) & remain(23 downto 0);
                when "10" =>
                    -- We use 2 bytes from the previous word, and two from the current word
                    wdata <= rdata(31 downto 16) & remain(15 downto 0);
                when "11" =>
                    -- We use 1 bytes from the previous word, and three from the current word
                    wdata <= rdata(31 downto  8) & remain( 7 downto 0);
                when others =>
                    wdata <= rdata;
                end case;
                if rdata_valid = '1' then
                    remain <= rdata;
                    wmask <= X"F";
                    wnext <= '1';
                    if last_word = '1' then
                        state <= idle;
                    end if;
                end if;
            
            when last =>
                case offset is
                when "01" =>
                    -- We use 3 bytes from the previous word, and one from the current word
                    wdata <= rdata(31 downto 24) & remain(23 downto 0);
                when "10" =>
                    -- We use 2 bytes from the previous word, and two from the current word
                    wdata <= rdata(31 downto 16) & remain(15 downto 0);
                when "11" =>
                    -- We use 1 bytes from the previous word, and three from the current word
                    wdata <= rdata(31 downto  8) & remain( 7 downto 0);
                when others =>
                    wdata <= rdata;
                end case;

                wmask <= X"F";
                state <= idle;

--                case last_bytes is
--                when "01" =>
--                    wmask <= "0001";
--                when "10" =>
--                    wmask <= "0011";
--                when "11" =>
--                    wmask <= "0111";
--                when others =>
--                    wmask <= "0000";
--                end case;
                
            when others =>
                null;
            end case;
            
            if reset = '1' then
                state <= idle;
            end if;
        end if;
    end process;

end arch;
