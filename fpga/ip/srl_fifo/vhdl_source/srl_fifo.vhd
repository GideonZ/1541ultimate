-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2004, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Small Synchronous Fifo Using SRL16
-------------------------------------------------------------------------------
-- File       : srl_fifo.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This implementation makes use of the SRL16 properties,
--              implementing a 16-deep synchronous fifo in only one LUT per
--              bit. It is a fall-through fifo.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

library unisim;
use unisim.vcomponents.all;

entity srl_fifo is
generic (Width : integer := 32;
         Depth : integer := 15; -- 15 is the maximum
         Threshold : integer := 13);
port (
    clock       : in std_logic;
    reset       : in std_logic;
    GetElement  : in std_logic;
    PutElement  : in std_logic;
    FlushFifo   : in std_logic;
    DataIn      : in std_logic_vector(Width-1 downto 0);
    DataOut     : out std_logic_vector(Width-1 downto 0);
    SpaceInFifo : out std_logic;
    AlmostFull  : out std_logic;
    DataInFifo  : out std_logic);
end SRL_fifo;

architecture  Gideon  of  srl_fifo  is

    signal NumElements     : std_logic_vector(3 downto 0);
    signal FilteredGet     : std_logic;
    signal FilteredPut     : std_logic;    
    signal DataInFifo_i    : std_logic;
    signal SpaceInFifo_i   : std_logic;
    constant Depth_std     : std_logic_vector(3 downto 0) := conv_std_logic_vector(Depth-1, 4);
begin
    FilteredGet <= DataInFifo_i and GetElement;
    FilteredPut <= SpaceInFifo_i and PutElement;
    DataInFifo  <= DataInFifo_i;
    SpaceInFifo <= SpaceInFifo_i;
    
    process(clock)
        variable NewCnt : std_logic_vector(3 downto 0);--integer range 0 to Depth;
    begin
        if rising_edge(clock) then
            if FlushFifo='1' then
                NewCnt := "1111"; --0;
            elsif (FilteredGet='1') and (FilteredPut='0') then
                NewCnt := NumElements - 1;
            elsif (FilteredGet='0') and (FilteredPut='1') then
                NewCnt := NumElements + 1;
            else
                NewCnt := NumElements;
            end if;
            NumElements <= NewCnt;

            if (NewCnt > Threshold) and (NewCnt /= "1111") then
                AlmostFull <= '1';
            else
                AlmostFull <= '0';
            end if;

            if (NewCnt = "1111") then
                DataInFifo_i <= '0';
            else
                DataInFifo_i <= '1';
            end if;

            if (NewCnt /= Depth_std) then
                SpaceInFifo_i <= '1';
            else
                SpaceInFifo_i <= '0';
            end if;                                    

            if Reset='1' then
                NumElements <= "1111";
                SpaceInFifo_i <= '1';
                DataInFifo_i <= '0';
                AlmostFull <= '0';
            end if;
        end if;
    end process;

    SRLs : for srl2 in 0 to Width-1 generate
      i_SRL : SRL16E
        port map (
          CLK => clock,
          CE  => FilteredPut,
          D   => DataIn(srl2),
          A3  => NumElements(3),
          A2  => NumElements(2),
          A1  => NumElements(1),
          A0  => NumElements(0),
          Q   => DataOut(srl2) );
    end generate;
end Gideon;
