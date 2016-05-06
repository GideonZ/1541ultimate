-------------------------------------------------------------------------------
-- Title      : rmii_transceiver_tb
-- Author     : Gideon Zweijtzer (gideon.zweijtzer@gmail.com)
-------------------------------------------------------------------------------
-- Description: Testbench for rmii transceiver
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
   
library work;

entity rmii_transceiver_tb is

end entity;

architecture testcase of rmii_transceiver_tb is
    signal clock               : std_logic := '0'; -- 50 MHz reference clock
    signal reset               : std_logic;
    signal rmii_crs_dv         : std_logic;
    signal rmii_rxd            : std_logic_vector(1 downto 0);
    signal rmii_tx_en          : std_logic;
    signal rmii_txd            : std_logic_vector(1 downto 0);
    signal eth_rx_data         : std_logic_vector(7 downto 0);
    signal eth_rx_sof          : std_logic;
    signal eth_rx_eof          : std_logic;
    signal eth_rx_valid        : std_logic;
    signal eth_tx_data         : std_logic_vector(7 downto 0);
    signal eth_tx_sof          : std_logic;
    signal eth_tx_eof          : std_logic;
    signal eth_tx_valid        : std_logic;
    signal eth_tx_ready        : std_logic;
    signal ten_meg_mode        : std_logic;

    type t_byte_array is array (natural range <>) of std_logic_vector(7 downto 0);

    constant c_frame_with_crc : t_byte_array := (
        X"00", X"0A", X"E6", X"F0", X"05", X"A3", X"00", X"12", X"34", X"56", X"78", X"90", X"08", X"00", X"45", X"00",
        X"00", X"30", X"B3", X"FE", X"00", X"00", X"80", X"11", X"72", X"BA", X"0A", X"00", X"00", X"03", X"0A", X"00",
        X"00", X"02", X"04", X"00", X"04", X"00", X"00", X"1C", X"89", X"4D", X"00", X"01", X"02", X"03", X"04", X"05",
        X"06", X"07", X"08", X"09", X"0A", X"0B", X"0C", X"0D", X"0E", X"0F", X"10", X"11", X"12", X"13"
--        , X"7A", X"D5", X"6B", X"B3"
        );

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.rmii_transceiver
    port map (
        clock        => clock,
        reset        => reset,
        rmii_crs_dv  => rmii_crs_dv,
        rmii_rxd     => rmii_rxd,
        rmii_tx_en   => rmii_tx_en,
        rmii_txd     => rmii_txd,
        eth_rx_data  => eth_rx_data,
        eth_rx_sof   => eth_rx_sof,
        eth_rx_eof   => eth_rx_eof,
        eth_rx_valid => eth_rx_valid,
        eth_tx_data  => eth_tx_data,
        eth_tx_sof   => eth_tx_sof,
        eth_tx_eof   => eth_tx_eof,
        eth_tx_valid => eth_tx_valid,
        eth_tx_ready => eth_tx_ready,
        ten_meg_mode => ten_meg_mode
    );
    
    rmii_crs_dv <= rmii_tx_en;
    rmii_rxd    <= rmii_txd;
    
    test: process    
        variable i : natural;
    begin
        eth_tx_data <= X"00";
        eth_tx_sof  <= '0';
        eth_tx_eof  <= '0';
        eth_tx_valid <= '0';
        ten_meg_mode <= '0';

        wait until reset = '0';
        wait until clock = '1';
        i := 0;
        L1: loop
            wait until clock = '1';
            if eth_tx_valid = '0' or eth_tx_ready = '1' then
                if eth_tx_eof = '1' then
                    eth_tx_valid <= '0';
                    eth_tx_data <= X"00";
                    exit L1;
                else                 
                    eth_tx_data <= c_frame_with_crc(i);
                    eth_tx_valid <= '1';
                    if i = c_frame_with_crc'left then
                        eth_tx_sof <= '1';
                    else
                        eth_tx_sof <= '0';
                    end if;
                    if i = c_frame_with_crc'right then
                        eth_tx_eof <= '1';
                    else
                        eth_tx_eof <= '0';
                    end if;
                    i := i + 1;
                end if;
            end if;
        end loop;

        wait;
    end process;

end architecture;
