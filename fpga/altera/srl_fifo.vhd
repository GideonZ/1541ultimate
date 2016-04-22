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
-- Altera wrapper to make the "SRL" fifo available to Altera tools.. Obviously
-- it does not use any SRL, but just BRAM!
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

LIBRARY altera_mf;
USE altera_mf.all;


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
end srl_fifo;

architecture  Altera  of  srl_fifo  is


    COMPONENT scfifo
    GENERIC (
        add_ram_output_register     : STRING;
        almost_full_value       : NATURAL;
        intended_device_family      : STRING;
        lpm_numwords        : NATURAL;
        lpm_showahead       : STRING;
        lpm_type        : STRING;
        lpm_width       : NATURAL;
        lpm_widthu      : NATURAL;
        overflow_checking       : STRING;
        underflow_checking      : STRING;
        use_eab     : STRING
    );
    PORT (
            clock   : IN STD_LOGIC ;
            data    : IN STD_LOGIC_VECTOR (Width-1 DOWNTO 0);
            rdreq   : IN STD_LOGIC ;
            sclr    : IN STD_LOGIC ;
            wrreq   : IN STD_LOGIC ;
            almost_full : OUT STD_LOGIC ;
            empty   : OUT STD_LOGIC ;
            full    : OUT STD_LOGIC ;
            q   : OUT STD_LOGIC_VECTOR (Width-1 DOWNTO 0);
            usedw   : OUT STD_LOGIC_VECTOR (3 DOWNTO 0)
    );
    END COMPONENT;

    signal full     : std_logic;
    signal empty    : std_logic;
    signal sclr     : std_logic;
BEGIN

    scfifo_component : scfifo
    GENERIC MAP (
        add_ram_output_register => "ON",
        almost_full_value => Threshold,
        intended_device_family => "Cyclone IV E",
        lpm_numwords => Depth,
        lpm_showahead => "ON",
        lpm_type => "scfifo",
        lpm_width => Width,
        lpm_widthu => 4,
        overflow_checking => "ON",
        underflow_checking => "ON",
        use_eab => "ON"
    )
    PORT MAP (
        clock => clock,
        data => DataIn,
        rdreq => GetElement,
        sclr => reset,
        wrreq => PutElement,
        empty => empty,
        full => full,
        q => DataOut,
        usedw => open );

    SpaceInFifo <= not full;
    DataInFifo  <= not empty;
    sclr <= reset or FlushFifo;

end Altera;
