library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;

entity ulpi_rx is
generic (
    g_support_split   : boolean := true;
    g_support_token   : boolean := true );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
                    
    rx_data         : in  std_logic_vector(7 downto 0);
    rx_last         : in  std_logic;
    rx_valid        : in  std_logic;
    rx_store        : in  std_logic;

    crc_sync        : out std_logic;
    crc_dvalid      : out std_logic;
    data_crc        : in  std_logic_vector(15 downto 0);

    status          : in  std_logic_vector(7 downto 0);
    usb_rx          : out t_usb_rx );

end ulpi_rx;

architecture gideon of ulpi_rx is
    type t_state is (idle, token0, token1, token2, check_token, resync,
                     data, data_check, handshake );
    signal state        : t_state;
    signal token_i      : std_logic_vector(18 downto 0) := (others => '0');
    signal token_crc    : std_logic_vector(4 downto 0)  := (others => '0');
    signal crc_tvalid   : std_logic;
    signal crc_sync_i   : std_logic;
    signal rx_valid_d   : std_logic;

    signal pid          : std_logic_vector(3 downto 0);
    signal valid_token  : std_logic;
    signal valid_split  : std_logic;
    signal valid_handsh : std_logic;
    signal valid_packet : std_logic;
    signal data_valid   : std_logic;
    signal data_start   : std_logic;
    signal data_out     : std_logic_vector(7 downto 0);
    signal error        : std_logic;
    signal rx_data_d1   : std_logic_vector(7 downto 0);
    signal rx_data_d2   : std_logic_vector(7 downto 0);
    signal rx_valid_d1  : std_logic;
    signal rx_valid_d2  : std_logic;
    signal recv_d       : std_logic_vector(1 to 3);

    --signal ipd_counter  : unsigned(tx_holdoff_delay'range) := (others => '0'); -- interpacket delay
begin
    usb_rx.token        <= vector_to_token(token_i(18 downto 8));
    usb_rx.split_token  <= vector_to_split_token(token_i(18 downto 0));
    usb_rx.pid          <= pid;
    usb_rx.valid_token  <= valid_token;
    usb_rx.valid_split  <= valid_split;
    usb_rx.valid_handsh <= valid_handsh;
    usb_rx.valid_packet <= valid_packet;
    usb_rx.data_valid   <= data_valid and rx_valid_d2;
    usb_rx.data_start   <= data_start;
    usb_rx.data         <= data_out;
    usb_rx.error        <= error;
    usb_rx.receiving    <= rx_store or recv_d(1) or recv_d(2) or recv_d(3) or status(4);
    
    process(clock)
    begin
        if rising_edge(clock) then
            error        <= '0';
            data_start   <= '0';
            valid_token  <= '0';
            valid_split  <= '0';
            valid_packet <= '0';
            valid_handsh <= '0';

--            if rx_store = '1' then -- reset interpacket delay counter for transmit
--                tx_holdoff <= '1';
--                ipd_counter <= tx_holdoff_delay;
--            else
--                if ipd_counter = 0 then
--                    tx_holdoff <= '0';
--                else
--                    ipd_counter <= ipd_counter - 1;
--                end if;
--            end if;

            recv_d  <= rx_store & recv_d(1 to 2);

            rx_data_d1   <= rx_data;
            if data_valid='1' then 
                rx_data_d2   <= rx_data_d1;
                data_out     <= rx_data_d2;
                rx_valid_d1  <= '1';
                rx_valid_d2  <= rx_valid_d1;
            end if;

            data_valid   <= '0';
            rx_valid_d   <= rx_valid;
            
            case state is
            when idle =>
                rx_valid_d1 <= '0';
                rx_valid_d2 <= '0';
                if rx_valid='1' and rx_store='1' then -- wait for first byte
                    if rx_data(7 downto 4) = not rx_data(3 downto 0) then
                        pid <= rx_data(3 downto 0);
                        if is_handshake(rx_data(3 downto 0)) then
                            if rx_last = '1' then
                                valid_handsh <= '1';
                            else
                                state <= handshake;
                            end if;                                
                        elsif is_token(rx_data(3 downto 0)) then
                            if g_support_token then
                                state <= token1;
                            else
                                error <= '1';
                            end if;
                        elsif is_split(rx_data(3 downto 0)) then
                            if g_support_split then
                                state <= token0;
                            else
                                error <= '1';
                            end if;
                        else
                            data_start <= '1';
                            state <= data;
                        end if;
                    else -- error in PID
                        error <= '1';
                    end if;
                end if; 

            when handshake =>
                if rx_store='1' then -- more data? error
                    error <= '1';
                    state <= resync;
                elsif rx_last = '1' then
                    valid_handsh <= '1';
                    state <= idle;
                end if;

            when token0 =>
                if rx_store='1' then
                    token_i(7 downto 0) <= rx_data;
                    state <= token1;
                end if;
                if rx_last='1' then -- should not occur here
                    error <= '1';
                    state <= resync;
                end if;                

            when token1 =>
                if rx_store='1' then
                    token_i(15 downto 8) <= rx_data;
                    state <= token2;
                end if;
                if rx_last='1' then -- should not occur here
                    error <= '1';
                    state <= resync;
                end if;                

            when token2 =>
                if rx_store='1' then
                    token_i(18 downto 16) <= rx_data(2 downto 0);
                    state <= check_token;
                end if;

            when data =>
                data_valid <= rx_store;
                if rx_last='1' then
                    state <= data_check;
                end if;

            when data_check =>
                if data_crc = X"4FFE" then
                    valid_packet <= '1';
                else
                    error <= '1';
                end if;
                state <= idle;

            when check_token =>
                if token_crc = "11001" then
                    if is_split(pid) then
                        valid_split <= '1';
                    else
                        valid_token <= '1';
                    end if;
                else
                    error <= '1';
                end if; 

                if rx_last='1' then
                    state <= idle;
                elsif rx_valid='0' then
                    state <= idle;
                else
                    state <= resync;
                end if;

            when resync =>
                if rx_last='1' then
                    state <= idle;
                elsif rx_valid='0' then
                    state <= idle;
                end if;
                
            when others =>
                null;
            end case;

            if reset = '1' then
                state <= idle;
                pid   <= X"0";
--                tx_holdoff <= '0';
            end if;
        end if;
    end process;

    r_token: if g_support_token or g_support_split generate
        i_token_crc: entity work.token_crc_check
        port map (
            clock       => clock,
            sync        => crc_sync_i,
            valid       => crc_tvalid,
            data_in     => rx_data,
            crc         => token_crc );
    end generate;

    crc_sync_i <= (rx_valid and rx_store) when state = idle else '0';
    crc_dvalid <= rx_store when state = data else '0';
    crc_tvalid <= rx_store when (state = token0) or (state = token1) or (state = token2) else '0';
    crc_sync   <= crc_sync_i;
end gideon;
