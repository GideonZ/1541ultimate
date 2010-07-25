library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ulpi_bus is
port (
    clock       : in    std_logic;
    reset       : in    std_logic;
    
    ULPI_DATA   : inout std_logic_vector(7 downto 0);
    ULPI_DIR    : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;
    
    -- status
    status      : out   std_logic_vector(7 downto 0);

    -- stream interface
    tx_data     : in    std_logic_vector(7 downto 0);
    tx_last     : in    std_logic;
    tx_valid    : in    std_logic;
    tx_start    : in    std_logic;
    tx_next     : out   std_logic;

    rx_data     : out   std_logic_vector(7 downto 0);
    rx_register : out   std_logic;
    rx_last     : out   std_logic;
    rx_valid    : out   std_logic;
    rx_store    : out   std_logic );

end ulpi_bus;
    
architecture gideon of ulpi_bus is
    signal ulpi_data_in     : std_logic_vector(7 downto 0);
    signal ulpi_data_out    : std_logic_vector(7 downto 0);
    signal ulpi_dir_d       : std_logic;
    signal ulpi_dir_d2      : std_logic;
    signal ulpi_nxt_d       : std_logic;
    signal ulpi_stop        : std_logic := '1';
    signal tx_int           : std_logic;
    signal reg_read         : std_logic;
    signal read_resp        : std_logic;
    signal ulpi_last        : std_logic;
    signal rx_valid_i       : std_logic;
    
    type t_state is (startup, idle, receive, transmit );
    signal state            : t_state;
    
    attribute iob : string;
    attribute iob of ulpi_data_in  : signal is "true";
    attribute iob of ulpi_dir_d    : signal is "true";
    attribute iob of ulpi_nxt_d    : signal is "true";
    attribute iob of ulpi_data_out : signal is "true";
    attribute iob of ULPI_STP      : signal is "true";
    
begin
    p_state: process(clock, reset)
    begin
        if rising_edge(clock) then
            ulpi_data_in <= ULPI_DATA;
            ulpi_dir_d   <= ULPI_DIR;
            ulpi_nxt_d   <= ULPI_NXT;
            ulpi_dir_d2  <= ulpi_dir_d;
            rx_valid_i   <= '0';
            rx_register  <= '0';
            rx_last      <= '0';
            rx_store     <= '0';
            rx_data      <= ulpi_data_in;
            rx_valid     <= rx_valid_i;
            ulpi_stop    <= '0';

            if ulpi_nxt_d='0' and reg_read='0' and ulpi_dir_d='1' and ulpi_dir_d2='1' then
                status <= ulpi_data_in;
            end if;

            case state is
            when startup =>
                ulpi_data_out <= X"00";
                if ulpi_dir_d = '0' then
                    ulpi_stop <= '0';
                    state <= idle;
                end if;

            when idle =>
                ulpi_data_out <= X"00";
                ulpi_last   <= '0';
                if ULPI_DIR = '1' then
                    state <= receive;
                elsif tx_valid = '1' and tx_start = '1' then
                    ulpi_stop     <= '0';
                    ulpi_data_out <= tx_data;
                    ulpi_last     <= tx_last;
                    state         <= transmit;
                    if tx_data(7 downto 6) = "11" then
                        reg_read <= '1';
                    else
                        reg_read <= '0';
                    end if;
                end if;

            when transmit =>
--                if ULPI_DIR = '1' then
--                    state <= receive;
--                    tx_int <= '1';
                if ULPI_NXT = '1' then
                    if ulpi_last = '1' or tx_valid = '0' then
                        ulpi_data_out <= X"00";
                        if reg_read='1' then
                            state <= idle;
                        else
                            ulpi_stop <= '1';
                            state     <= idle;
                        end if;
                    else
                        ulpi_data_out <= tx_data;
                        ulpi_last     <= tx_last;
                    end if;
--                elsif tx_valid = '0' then
--                    state <= idle;
                end if;
            
            when receive =>
                rx_valid_i   <= '1';
                rx_store     <= rx_valid_i and ulpi_nxt_d and not reg_read;

                if ULPI_DIR = '0' then
                    if tx_int = '1' then
                        tx_int <= '0';
                        state <= transmit;
                    else
                        state <= idle;
                    end if;
                    rx_valid_i <= '0';
                    if reg_read='1' then
                        rx_register  <= '1';
                        reg_read     <= '0';
                    else
                        rx_last    <= '1';
                    end if;
                end if;

            when others =>
                null;

            end case;
            
            if reset='1' then
                state     <= startup;
                tx_int    <= '0';
                reg_read  <= '0';
                read_resp <= '0';
                ulpi_stop <= '0';
                status    <= (others => '0');
            end if;
        end if;
--        if reset = '1' then -- asynchronous reset
--            ulpi_stop <= '1';
--        end if;
    end process;

    p_next: process(state, tx_valid, tx_start, ULPI_DIR, ULPI_NXT, ulpi_last)
    begin
        case state is
        when idle =>
            tx_next <= not ULPI_DIR and tx_valid and tx_start;

        when transmit =>
            tx_next <= ULPI_NXT and tx_valid and not ulpi_last;
        
        when others =>
            tx_next <= '0';
        
        end case;            
    end process;

    ULPI_STP   <= ulpi_stop;
    ULPI_DATA  <= ulpi_data_out when ULPI_DIR='0' and ulpi_dir_d='0' else (others => 'Z');
    
end gideon;    
    