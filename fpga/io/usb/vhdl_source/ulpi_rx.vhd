library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;

entity ulpi_rx is
generic (
    g_allow_token   : boolean := true );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
                    
    rx_data         : in  std_logic_vector(7 downto 0);
    rx_last         : in  std_logic;
    rx_valid        : in  std_logic;
    rx_store        : in  std_logic;
                    
    pid             : out std_logic_vector(3 downto 0);
    valid_token     : out std_logic;
    valid_handsh    : out std_logic;
    token           : out std_logic_vector(10 downto 0);

    valid_packet    : out std_logic;
    data_valid      : out std_logic;
    data_start      : out std_logic;
    data_out        : out std_logic_vector(7 downto 0);

    error           : out std_logic );

end ulpi_rx;

architecture gideon of ulpi_rx is
    type t_state is (idle, token1, token2, check_token, check_token2, resync,
                     data, data_check, handshake );
    signal state        : t_state;
    signal token_i      : std_logic_vector(10 downto 0) := (others => '0');
    signal token_crc    : std_logic_vector(4 downto 0)  := (others => '0');
    signal crc_in       : std_logic_vector(4 downto 0);
    signal crc_dvalid   : std_logic;
    signal crc_sync     : std_logic;
    signal data_crc     : std_logic_vector(15 downto 0);

begin
    token    <= token_i;
    data_out <= rx_data;
    data_valid <= rx_store when state = data else '0';

    process(clock)
    begin
        if rising_edge(clock) then
            data_start <= '0';
            error      <= '0';
            valid_token  <= '0';
            valid_packet <= '0';
            valid_handsh <= '0';
            
            case state is
            when idle =>
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
                            if g_allow_token then
                                state <= token1;
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

            when token1 =>
                if rx_store='1' then
                    token_i(7 downto 0) <= rx_data;
                    state <= token2;
                end if;
                if rx_last='1' then -- should not occur here
                    error <= '1';
                    state <= idle; -- good enough?
                end if;                

            when token2 =>
                if rx_store='1' then
                    token_i(10 downto 8) <= rx_data(2 downto 0);
                    crc_in <= rx_data(7 downto 3);
                    state <= check_token;
                end if;

            when data =>
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
                state <= check_token2; -- delay
                
            when check_token2 =>
                if crc_in = token_crc then
                    valid_token <= '1';
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
            end if;
        end if;
    end process;

    r_token: if g_allow_token generate
        i_token_crc: entity work.token_crc
        port map (
            clock       => clock,
            sync        => '1',
            token_in    => token_i,
            crc         => token_crc );
    end generate;

    crc_sync <= '1' when state = idle else '0';
    crc_dvalid <= rx_store when state = data else '0';
    
    i_data_crc: entity work.data_crc
    port map (
        clock       => clock,
        sync        => crc_sync,
        valid       => crc_dvalid,
        data_in     => rx_data,
        
        crc         => data_crc );
    
end gideon;
