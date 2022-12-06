-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2022, Gideon's Logic B.V.
--
-------------------------------------------------------------------------------
-- Title      : Small Synchronous Fifo Using SRL16
-------------------------------------------------------------------------------
-- File       : srl_fifo.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This implementation is a simple wrapper around a standard
--              fifo component for backwards compatibility
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

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
end entity;

architecture  wrapper  of  srl_fifo  is
    signal full     : std_logic;
begin
    SpaceInFifo <= not full;
    
    i_fifo: entity work.sync_fifo
    generic map (
        g_depth        => 15,
        g_data_width   => Width,
        g_threshold    => Threshold,
        g_storage      => "distributed",
        g_storage_lat  => "distributed",
        g_fall_through => true
    )
    port map (
        clock          => clock,
        reset          => reset,
        rd_en          => GetElement,
        wr_en          => PutElement,
        din            => DataIn,
        dout           => DataOut,
        flush          => FlushFifo,
        full           => full,
        almost_full    => AlmostFull,
        empty          => open,
        valid          => DataInFifo,
        count          => open
    );

end architecture;
