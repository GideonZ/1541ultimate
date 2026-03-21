library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ulpi_bus is
port (
    clock       : in    std_logic;
    reset       : in    std_logic;
    
    ULPI_DATA_I : in    std_logic_vector(7 downto 0);
    ULPI_DATA_O : out   std_logic_vector(7 downto 0);
    ULPI_DATA_T : out   std_logic;
    ULPI_DIR    : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;
    
    -- status
    status      : out   std_logic_vector(7 downto 0);
    operational : in    std_logic := '1';
    
    -- chirp interface
    do_chirp    : in    std_logic := '0';
    chirp_data  : in    std_logic := '0';
    
    -- register interface
    reg_read    : in    std_logic;
    reg_write   : in    std_logic;
    reg_address : in    std_logic_vector(5 downto 0);
    reg_wdata   : in    std_logic_vector(7 downto 0);
    reg_ack     : out   std_logic;
    
    -- stream interface
    tx_data     : in    std_logic_vector(7 downto 0);
    tx_last     : in    std_logic;
    tx_valid    : in    std_logic;
    tx_start    : in    std_logic;
    tx_next     : out   std_logic;

    -- for the tracer
    rx_cmd      : out   std_logic;
    rx_ourdata  : out   std_logic;

    rx_data     : out   std_logic_vector(7 downto 0);
    rx_register : out   std_logic;
    rx_last     : out   std_logic;
    rx_valid    : out   std_logic;
    rx_store    : out   std_logic );

    attribute keep_hierarchy : string;
    attribute keep_hierarchy of ulpi_bus : entity is "yes";

end ulpi_bus;
    
architecture gideon of ulpi_bus is
    signal ulpi_data_out    : std_logic_vector(7 downto 0);

    signal ulpi_data_in     : std_logic_vector(7 downto 0);
    signal ulpi_stp_d1      : std_logic; -- for signaltap only
    signal ulpi_dir_d1      : std_logic;
    signal ulpi_dir_d2      : std_logic;
    signal ulpi_dir_d3      : std_logic;
    signal ulpi_nxt_d1      : std_logic;
    signal ulpi_nxt_d2      : std_logic;
    signal ulpi_nxt_d3      : std_logic;
    signal reg_cmd_d2       : std_logic;
    signal reg_cmd_d3       : std_logic;
    signal rx_reg_i         : std_logic;
    signal tx_reg_i         : std_logic;
    signal rx_status_i      : std_logic;

    signal ulpi_stop        : std_logic := '1';
    signal ulpi_last        : std_logic;
    signal bus_has_our_data : std_logic;
        
    type t_state is ( idle, chirp, reading, writing, writing_data, transmit );
    signal state            : t_state;
    
    attribute iob : string;
    attribute iob of ulpi_data_in  : signal is "false";
    attribute iob of ulpi_dir_d1   : signal is "false";
    attribute iob of ulpi_nxt_d1   : signal is "false";
    attribute iob of ulpi_data_out : signal is "true";
    attribute iob of ulpi_stop     : signal is "true";

begin
    -- Marking incoming data based on next/dir pattern
    rx_data      <= ulpi_data_in;
    rx_store     <= ulpi_dir_d1 and ulpi_dir_d2 and ulpi_nxt_d1 and operational;
    rx_valid     <= ulpi_dir_d1 and ulpi_dir_d2;
    rx_last      <= not ulpi_dir_d1 and ulpi_dir_d2;
    rx_status_i  <= ulpi_dir_d1 and ulpi_dir_d2 and not ulpi_nxt_d1 and not rx_reg_i;
    rx_reg_i     <= (ulpi_dir_d1 and ulpi_dir_d2 and not ulpi_dir_d3) and
                    (not ulpi_nxt_d1 and not ulpi_nxt_d2 and ulpi_nxt_d3) and
                    reg_cmd_d3;

    rx_cmd       <= rx_status_i;
    rx_ourdata   <= not ulpi_dir_d1 and ulpi_nxt_d1; -- next = 1 and dir = 0, same delay as rx_data (1)
    
    rx_register  <= rx_reg_i;
    reg_ack      <= rx_reg_i or tx_reg_i;
    
    p_sample: process(clock, reset)
    begin
        if rising_edge(clock) then
            ulpi_data_in <= ULPI_DATA_I;

            reg_cmd_d2   <= ulpi_data_in(7) and ulpi_data_in(6);
            reg_cmd_d3   <= reg_cmd_d2;

            ulpi_stp_d1  <= ulpi_stop;

            ulpi_dir_d1  <= ULPI_DIR;
            ulpi_dir_d2  <= ulpi_dir_d1;
            ulpi_dir_d3  <= ulpi_dir_d2;

            ulpi_nxt_d1  <= ULPI_NXT;
            ulpi_nxt_d2  <= ulpi_nxt_d1;
            ulpi_nxt_d3  <= ulpi_nxt_d2;

            if rx_status_i='1' then
                status <= ulpi_data_in;
            end if;

            if reset='1' then
                status <= (others => '0');
            end if;
        end if;
    end process;

    p_tx_state: process(clock, reset)
    begin
        if rising_edge(clock) then
            ulpi_stop    <= '0';
            tx_reg_i     <= '0';

            case state is

            when idle =>
                ulpi_data_out <= X"00";
                if reg_read='1' and rx_reg_i='0' then
                    ulpi_data_out <= "11" & reg_address;
                    state <= reading;
                elsif reg_write='1' and tx_reg_i='0' then
                    ulpi_data_out <= "10" & reg_address;
                    state <= writing;
                elsif do_chirp='1' then
                    if ULPI_DIR='0' then
                        ulpi_last     <= '0';
                        state         <= chirp;
                    end if;
                    ulpi_data_out <= X"40"; -- PIDless packet
                elsif tx_valid = '1' and tx_start = '1' then
                    if ULPI_DIR='0' then
                        ulpi_last     <= tx_last;
                        state         <= transmit;
                    end if;
                    ulpi_data_out <= tx_data;
                end if;

            when chirp =>
                if ULPI_NXT = '1' then
                    if do_chirp = '0' then
                        ulpi_data_out <= X"00";
                        ulpi_stop     <= '1';
                        state         <= idle;
                    else
                        ulpi_data_out <= (others => chirp_data);
                    end if;
                end if;
                    
            when reading =>
                if rx_reg_i='1' then
                    ulpi_data_out <= X"00";
                    state <= idle;
                end if;
                if ulpi_dir_d1='1' then
                    state <= idle; -- terminate current tx
                    ulpi_data_out <= X"00";
                end if;

            when writing =>
                if ULPI_DIR='1' then
                    state <= idle; -- terminate current tx
                    ulpi_data_out <= X"00";
                elsif ULPI_NXT='1' then
                    ulpi_data_out <= reg_wdata;
                    state <= writing_data;
                end if;
            
            when writing_data =>
                if ULPI_DIR='1' then
                    state <= idle; -- terminate current tx
                    ulpi_data_out <= X"00";
                elsif ULPI_NXT='1' then
                    ulpi_data_out <= X"00";
                    tx_reg_i <= '1';
                    ulpi_stop <= '1';
                    state <= idle;
                end if;
                
            when transmit =>
                if ULPI_NXT = '1' then
                    if ulpi_last='1' or tx_valid = '0' then
                        ulpi_data_out <= X"00";
                        ulpi_stop     <= '1';
                        state         <= idle;
                    else
                        ulpi_data_out <= tx_data;
                        ulpi_last     <= tx_last;
                    end if;
                end if;
            
            when others =>
                null;

            end case;

            if reset='1' then
                state     <= idle;
                ulpi_stop <= '1';
                ulpi_last <= '0';
            end if;
        end if;
    end process;

    p_next: process(state, tx_valid, tx_start, rx_reg_i, tx_reg_i, ULPI_DIR, ULPI_NXT, ulpi_last, reg_read, reg_write, bus_has_our_data)
    begin
        case state is
        when idle =>
            tx_next <= not ULPI_DIR and tx_valid and tx_start; -- first byte is transferred to register
            if reg_read='1' and rx_reg_i='0' then
                tx_next <= '0';
            end if;
            if reg_write='1' and tx_reg_i='0' then
                tx_next <= '0';
            end if;

        when transmit =>
            tx_next <= ULPI_NXT and bus_has_our_data and tx_valid and not ulpi_last; -- phy accepted this data.
        
        when others =>
            tx_next <= '0';
        
        end case;            
    end process;

    bus_has_our_data <= '1' when ULPI_DIR='0' and ulpi_dir_d1='0' else '0';    
    -- bus_has_our_data <= '1' when ulpi_dir_d1 = '0' else '0'; -- Eliminate the loop from ULPI_DIR

    ULPI_DATA_O <= ulpi_data_out;
    ULPI_DATA_T <= not ULPI_DIR; -- when falling_edge(clock); --bus_has_our_data;
    ULPI_STP    <= ulpi_stop;
end gideon;    
