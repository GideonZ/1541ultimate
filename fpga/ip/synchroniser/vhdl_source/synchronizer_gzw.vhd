-------------------------------------------------------------------------------
-- Title      : Vector Synchronizer block
-- Author     : Gideon Zweijtzer (gideon.zweijtzer@gmail.com)
-------------------------------------------------------------------------------
-- Description: Synchroniser block implementing a better synchronizer.
--
-- TIMING CONSTRAINTS
--
-- For Altera users:
-- Add the following lines to your SDC file to add false path constraints to
-- the required paths:
--
-- set_false_path \
--     -from [get_registers *\|synchronizer_gzw:*\|*_tig_src] \
--     -to [get_registers *\|synchronizer_gzw:*\|*_tig_dst]
--
-- For Xilinx users:
-- Add the following lines to your UCF file to add timing ignore attributes to
-- the required paths:
--
-- INST "*_tig_src*"      TNM = "tnm_sync_src";
-- INST "*_tig_dst*"      TNM = "tnm_sync_dst";
-- TIMESPEC "ts_sync_tig" = FROM "tnm_sync_src" TO "tnm_sync_dst"  TIG;
--
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;

entity synchronizer_gzw is
    generic (
        g_width     : natural := 16;
        g_fast      : boolean := false );

    port (
        tx_clock    : in  std_logic;
        tx_push     : in  std_logic;
        tx_data     : in  std_logic_vector(g_width - 1 downto 0) := (others => '0');
        tx_done     : out std_logic;
        rx_clock    : in  std_logic;
        rx_new_data : out std_logic;
        rx_data     : out std_logic_vector(g_width - 1 downto 0) := (others => '0')
    );

    ---------------------------------------------------------------------------
    -- synthesis attributes to prevent duplication and balancing.
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_duplication                        : string;
    attribute register_duplication of synchronizer_gzw    : entity is "no";
    attribute register_balancing                          : string;
    attribute register_balancing of synchronizer_gzw      : entity is "no";
    -- Altera attributes
    attribute dont_replicate                              : boolean;
    attribute dont_replicate of synchronizer_gzw          : entity is true;
    attribute dont_retime                                 : boolean;
    attribute dont_retime of synchronizer_gzw             : entity is true;
    -----------------------------------------------------------------------------

end entity;

architecture rtl of synchronizer_gzw is
    signal tx_inhibit       : std_logic := '0';
    signal tx_enable        : std_logic := '0';
    signal tx_done_i        : std_logic := '0';
    signal tx_tig_src       : std_logic := '0';
    
    signal rx_tig_dst       : std_logic := '0';
    signal rx_tig_src       : std_logic := '0';
    signal rx_stable        : std_logic := '0';
    signal rx_done          : std_logic := '0';
    signal tx_tig_dst       : std_logic := '0';
    signal tx_stable        : std_logic := '0';
    signal tx_stable_d      : std_logic := '0';     

    signal tx_data_tig_src  : std_logic_vector(tx_data'range) := (others => '0');
    signal rx_data_tig_dst  : std_logic_vector(tx_data'range) := (others => '0');
begin 
    tx_enable  <= tx_push and not tx_inhibit;
    tx_inhibit <= tx_tig_src xor tx_stable; 

    p_tx: process(tx_clock)
    begin
        if rising_edge(tx_clock) then
            -- path to receive side
            tx_tig_src  <= tx_tig_src xor tx_enable; -- toggle flipfop
            if tx_enable = '1' then
                tx_data_tig_src <= tx_data;
            end if;

            -- path from receive side
            tx_stable   <= tx_tig_dst;
            tx_stable_d <= tx_stable;
            if not g_fast then
                tx_tig_dst <= rx_tig_src;
            end if;
        end if;
        if falling_edge(tx_clock) then
            if g_fast then
                tx_tig_dst <= rx_tig_src;
            end if;
        end if;
    end process;
    
    tx_done_i <= tx_stable xor tx_stable_d;
    tx_done <= tx_done_i;
    
    p_rx: process(rx_clock)
    begin
        if rising_edge(rx_clock) then
            -- path from transmit side
            if not g_fast then
                rx_tig_dst <= tx_tig_src;
            end if;
            rx_stable   <= rx_tig_dst;
            rx_tig_src  <= rx_stable; -- rx_tig_src = stable_d            
            if rx_done = '1' then
                rx_data_tig_dst <= tx_data_tig_src;
            end if;
            rx_new_data <= rx_done;
        end if;
        if falling_edge(rx_clock) then
            if g_fast then
                rx_tig_dst <= tx_tig_src;
            end if;
        end if;
    end process;

    rx_done <= rx_tig_src xor rx_stable;
    rx_data <= rx_data_tig_dst;

end rtl;
