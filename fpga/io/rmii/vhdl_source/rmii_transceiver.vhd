-------------------------------------------------------------------------------
-- Title      : rmii_transceiver
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Receiver / transmitter for RMII
--              TODO: Implement 10 Mbps mode
--
--              NOTE: The receive stream will include the FCS, and also an
--              additional byte, which indicates the correctness of the FCS.
--              This byte will be FF when correct and 00 when incorrect. It
--              coincides with eof. So CRC checking is done, CRC stripping is
--              not. The transmit path appends CRC after receiving the last
--              byte through the stream bus (thus after eof).
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;


entity rmii_transceiver is
port (
    clock               : in  std_logic; -- 50 MHz reference clock
    reset               : in  std_logic;

    rmii_crs_dv         : in  std_logic;
    rmii_rxd            : in  std_logic_vector(1 downto 0);
    rmii_tx_en          : out std_logic;
    rmii_txd            : out std_logic_vector(1 downto 0);

    -- stream bus alike interface to MAC
    eth_rx_data         : out std_logic_vector(7 downto 0);
    eth_rx_sof          : out std_logic;
    eth_rx_eof          : out std_logic;
    eth_rx_valid        : out std_logic;
    
    eth_tx_data         : in  std_logic_vector(7 downto 0);
    eth_tx_sof          : in  std_logic;
    eth_tx_eof          : in  std_logic;
    eth_tx_valid        : in  std_logic;
    eth_tx_ready        : out std_logic;

    -- Mode switch
    ten_meg_mode        : in  std_logic );

end entity;

architecture rtl of rmii_transceiver is
    type t_state is (idle, preamble, data0, data1, data2, data3, crc);
    signal rx_state : t_state;
    signal tx_state : t_state;

    signal crs_dv   : std_logic;
    signal rxd      : std_logic_vector(1 downto 0);
    signal rx_valid : std_logic;
    signal rx_first : std_logic;
    signal rx_end   : std_logic;
    signal rx_shift : std_logic_vector(7 downto 0) := (others => '0');
    signal bad_carrier  : std_logic;

    signal rx_crc_sync  : std_logic;
    signal rx_crc_dav   : std_logic;
    signal rx_crc_data  : std_logic_vector(3 downto 0) := (others => '0');
    signal rx_crc       : std_logic_vector(31 downto 0);
    signal crc_ok       : std_logic;
    
    signal tx_count : natural range 0 to 15;
    signal tx_crc_dav   : std_logic;
    signal tx_crc_sync  : std_logic;
    signal tx_crc_data  : std_logic_vector(1 downto 0);
    signal tx_crc       : std_logic_vector(31 downto 0);
begin
    p_receive: process(clock)
    begin
        if rising_edge(clock) then
            -- synchronize
            crs_dv <= rmii_crs_dv;
            rxd    <= rmii_rxd;
            bad_carrier <= '0';
            rx_valid    <= '0';            
            rx_end      <= '0';
            rx_crc_dav  <= '0';            
            eth_rx_valid <= '0';
            
            if rx_valid = '1' or rx_end = '1' then
                eth_rx_eof <= rx_end;
                eth_rx_sof <= rx_first;
                if rx_end = '1' then
                    eth_rx_data <= (others => crc_ok);
                else
                    eth_rx_data <= rx_shift;
                end if;
                eth_rx_valid <= '1';
                rx_first <= '0';
            end if;

            case rx_state is
            when idle =>
                if crs_dv = '1' then
                    if rxd = "01" then
                        rx_state <= preamble;
                    elsif rxd = "10" then
                        bad_carrier <= '1';
                    end if;
                end if; 

            when preamble =>
                rx_first <= '1';
                if crs_dv = '0' then
                    rx_state <= idle;
                else -- dv = 1
                    if rxd = "11" then
                        rx_state <= data0;
                    elsif rxd = "01" then
                        rx_state <= preamble;
                    else
                        bad_carrier <= '1';
                        rx_state <= idle;
                    end if;
                end if;
                
            when data0 => -- crs_dv = CRS
                rx_shift(1 downto 0) <= rxd;
                rx_state <= data1;
            
            when data1 => -- crs_dv = DV
                rx_shift(3 downto 2) <= rxd;
                rx_state <= data2;
                if crs_dv = '0' then
                    rx_end <= '1';
                    rx_state <= idle;
                else
                    rx_crc_dav <= '1';
                    rx_crc_data <= rxd & rx_shift(1 downto 0);
                end if;                
                
            when data2 => -- crs_dv = CRS
                rx_shift(5 downto 4) <= rxd;
                rx_state <= data3;
            
            when data3 => -- crs_dv = DV
                rx_shift(7 downto 6) <= rxd;
                rx_crc_dav <= '1';
                rx_crc_data <= rxd & rx_shift(5 downto 4);
                rx_state <= data0;
                rx_valid <= '1';

            when others =>
                null;
            end case;

            if reset='1' then
                eth_rx_sof <= '0';
                eth_rx_eof <= '0';
                eth_rx_data <= (others => '0');
                rx_first <= '0';
                rx_state <= idle;
            end if;
        end if;
    end process;

    rx_crc_sync <= '1' when rx_state = preamble else '0';

    i_receive_crc: entity work.crc32
        generic map (
            g_data_width => 4
        )
        port map(
            clock        => clock,
            clock_en     => '1',
            sync         => rx_crc_sync,
            data         => rx_crc_data,
            data_valid   => rx_crc_dav,
            crc          => rx_crc
        );
    crc_ok   <= '1' when (rx_crc = X"2144DF1C") else '0';

    p_transmit: process(clock)
    begin
        if rising_edge(clock) then
            case tx_state is
            when idle =>
                rmii_tx_en <= '0';
                rmii_txd <= "00";

                if eth_tx_valid = '1' then
                    tx_state <= preamble;
                    tx_count <= 13;
                end if; 

            when preamble =>
                rmii_tx_en <= '1';
                if tx_count = 0 then
                    rmii_txd <= "11";
                    tx_state <= data0;
                else
                    rmii_txd <= "01";
                    tx_count <= tx_count - 1;
                end if;
                
            when data0 =>
                if eth_tx_valid = '0' then
                    tx_state <= idle;
                    rmii_tx_en <= '0';
                    rmii_txd <= "00";
                else
                    rmii_tx_en <= '1';
                    rmii_txd <= eth_tx_data(1 downto 0);
                    tx_state <= data1;                
                end if;

            when data1 =>
                rmii_tx_en <= '1';
                rmii_txd <= eth_tx_data(3 downto 2);
                tx_state <= data2;                

            when data2 =>
                rmii_tx_en <= '1';
                rmii_txd <= eth_tx_data(5 downto 4);
                tx_state <= data3;                

            when data3 =>
                tx_count <= 15;
                rmii_tx_en <= '1';
                rmii_txd <= eth_tx_data(7 downto 6);
                if eth_tx_eof = '1' then
                    tx_state <= crc;
                else
                    tx_state <= data0;
                end if;

            when crc =>
                rmii_tx_en <= '1';
                rmii_txd <= tx_crc(31 - tx_count*2 downto 30 - tx_count*2);                
                if tx_count = 0 then
                    tx_state <= idle;
                else
                    tx_count <= tx_count - 1;
                end if;

            when others =>
                null;                

            end case;

            if reset='1' then
                rmii_tx_en <= '0';
                rmii_txd <= "00";
                tx_state <= idle;
            end if;
        end if;
    end process;

    eth_tx_ready <= '1' when tx_state = data3 else '0';
    
    tx_crc_sync <= '1' when tx_state = preamble else '0';

    with tx_state select tx_crc_data <=
        eth_tx_data(1 downto 0) when data0,
        eth_tx_data(3 downto 2) when data1,
        eth_tx_data(5 downto 4) when data2,
        eth_tx_data(7 downto 6) when data3,
        "00" when others;
                     
    with tx_state select tx_crc_dav <=
        '1' when data0 | data1 | data2 | data3,
        '0' when others;

    i_transmit_crc: entity work.crc32
        generic map (
            g_data_width => 2
        )
        port map(
            clock        => clock,
            clock_en     => '1',
            sync         => tx_crc_sync,
            data         => tx_crc_data,
            data_valid   => tx_crc_dav,
            crc          => tx_crc
        );

end architecture;
