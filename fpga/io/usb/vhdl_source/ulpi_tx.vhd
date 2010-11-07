library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ulpi_tx is
port (
    clock       : in    std_logic;
    reset       : in    std_logic;
    
    -- Bus Interface
    tx_start    : out   std_logic;
    tx_last     : out   std_logic;
    tx_valid    : out   std_logic;
    tx_next     : in    std_logic;
    tx_data     : out   std_logic_vector(7 downto 0);
    rx_data     : in    std_logic_vector(7 downto 0);
    
    -- Status
    status      : in    std_logic_vector(7 downto 0);
    speed       : in    std_logic_vector(1 downto 0);
    gap_length  : in    std_logic_vector(7 downto 0);
    busy        : out   std_logic;
    tx_ack      : out   std_logic;
    
    -- Interface to send tokens and handshakes
    send_token  : in    std_logic;
    send_handsh : in    std_logic;
    pid         : in    std_logic_vector(3 downto 0);
    token       : in    std_logic_vector(10 downto 0);
    
    -- Interface to send data packets
    send_data   : in    std_logic;
    no_data     : in    std_logic := '0';
    user_data   : in    std_logic_vector(7 downto 0);
    user_last   : in    std_logic;
    user_valid  : in    std_logic;
    user_next   : out   std_logic;
            
    -- Interface to output reset packets
    send_reset_data : in std_logic;
    reset_last      : in std_logic;
    reset_data      : in std_logic_vector(7 downto 0) );

end ulpi_tx;

architecture gideon of ulpi_tx is
    type t_state is (idle, crc_1, crc_2, token1, token2, token3,
                     reset_pkt, transmit,
                     wait4next, write_end, handshake, gap, gap2);
    signal state    : t_state;

--    type t_tx_type is (none, dat, hsh, tok);
--    signal tx_type      : t_tx_type := none;
    
    signal j_state      : std_logic_vector(1 downto 0);
    signal tx_data_i    : std_logic_vector(7 downto 0);
    signal tx_last_i    : std_logic;
    signal token_crc    : std_logic_vector(4 downto 0);
    signal crc_dvalid   : std_logic;
    signal crc_sync     : std_logic;
    signal data_crc     : std_logic_vector(15 downto 0);
    signal no_data_d    : std_logic;
--    constant c_gap_val  : integer := 0;
    signal gap_count    : integer range 0 to 255;

    signal debug_count  : integer range 0 to 255 := 0;
    signal debug_error  : std_logic := '0';

    -- XILINX USB STICK:
    -- On high speed, gap values 0x05 - 0x25 WORK.. (bigger than 0x25 doesn't, smaller than 0x05 doesn't..)
    -- TRUST USB 2.0 Hub:
    -- On high speed, gap values 0x07 - 0x1D WORK.. with the exception of 0x09.
    -- Samsung DVD-Burner:
    -- On high speed, gap values 0x00 - 0x23 WORK.. with the exception of 0x04.
    -- Western Digital external HD:
    -- On high speed, gap values 0x05 - 0x21 WORK.. with the exception of 0x06 and 0x09.
    -- 
    
    attribute fsm_encoding : string;
    attribute fsm_encoding of state : signal is "sequential";
begin
    tx_ack <= (send_token or send_handsh or send_data)  when
              (state = idle) else '0';
    
    j_state <= not speed(0) & speed(0);

    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                tx_start  <= '0';
                tx_valid  <= '0';
                tx_last_i <= '0';
                tx_data_i <= X"00";
                no_data_d <= no_data;

                if send_token='1' then
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data_i <= X"4" & pid;
                    state <= token1;
                elsif send_handsh='1' then
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data_i <= X"4" & pid;
                    tx_last_i <= '1';
                    state <= handshake;
                elsif send_data='1' then
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data_i <= X"4" & pid;
                    state <= wait4next;
                elsif send_reset_data='1' then
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data_i <= X"40"; -- pidless
                    state <= reset_pkt;
                end if;             
            
            when wait4next =>
                if tx_next='1' then
                    tx_start <= '0';
                    tx_valid <= '1';
                    if no_data_d='1' then
                        state <= crc_1;
                    else
                        state <= transmit;
                    end if;
                end if;

            when handshake =>
                if tx_next='1' then
                    tx_start  <= '0';
                    tx_valid  <= '0';
                    tx_last_i <= '0';
                    state <= gap;
                end if;

            when write_end =>
                if tx_next='1' then
                    tx_start  <= '0';
                    tx_valid  <= '0';
                    tx_last_i <= '0';
                    state <= idle;
                end if;
                
            when crc_1 =>
                if tx_next = '1' then
                    tx_last_i <= '1';
                    state <= crc_2;
                end if;
            
            when crc_2 =>
                if tx_next = '1' then
                    tx_last_i <= '0';
                    tx_valid  <= '0';
                    state <= gap;
                end if;
                
            when token1 =>
                if tx_next = '1' then
                    tx_start  <= '0';
                    tx_data_i <= token(7 downto 0);
                    state     <= token2;
                end if;
            
            when token2 =>
                if tx_next = '1' then
                    tx_data_i <= token_crc & token(10 downto 8);
                    tx_last_i   <= '1';
                    state     <= token3;
                end if;

            when token3 =>
                if tx_next = '1' then
                    tx_last_i <= '0';
                    tx_valid  <= '0';
                    state <= gap;
                end if;

            when gap =>
                gap_count <= to_integer(unsigned(gap_length));
                if speed(1)='0' then
                    if status(1 downto 0)="00" or status(1 downto 0)="11" then --<-- :-o SE1 in low speed?! 
                        state <= gap2;
                    end if;
                else -- high speed
                    state <= gap2;
                end if;
                
            when gap2 =>
                if speed(1)='0' then
                    if status(1 downto 0) = j_state then
                        state <= idle;
                    end if;
                else
                    if gap_count = 0 then
                        state <= idle;
                    else
                        gap_count <= gap_count - 1;
                    end if;
                end if;

            when transmit =>
                if tx_next='1' and user_last='1' then
                    state <= crc_1;
                end if;

            when reset_pkt =>
                tx_data_i <= reset_data;
                if reset_last='1' then
                    tx_last_i <= '1';
                end if;
                if tx_next='1' then
                    tx_start <= '0';
                    if tx_last_i='1' then
                        tx_valid <= '0';
                        tx_last_i <= '0';
                        state <= idle;
                    end if;
                end if;
                
            when others => 
                null;

            end case;                                    

---------------------------------------------------
--          DEBUG
---------------------------------------------------
            if state /= token2 then
                debug_count <= 0;
                debug_error <= '0';
            elsif debug_count = 255 then
                debug_error <= '1';
            else
                debug_count <= debug_count + 1;
            end if;
            if debug_error='1' then -- this makes debug_error continue to exist
                state <= idle;
            end if;
---------------------------------------------------
            if reset='1' then
                state  <= idle;
            end if;
        end if;
    end process;

    crc_dvalid   <= '1' when (state = transmit) and tx_next='1' else '0';
    crc_sync     <= '1' when (state = idle) else '0';
    busy         <= '0' when (state = idle) else '1'; -- or (state = gap) else '1';
        
    i_token_crc: entity work.token_crc
    port map (
        clock       => clock,
        sync        => '1',
        token_in    => token,
        crc         => token_crc );

    i_data_crc: entity work.data_crc
    port map (
        clock       => clock,
        sync        => crc_sync,
        valid       => crc_dvalid,
        data_in     => user_data,
        
        crc         => data_crc );

    with state select tx_data <=
        user_data             when transmit,
        data_crc(7 downto 0)  when crc_1,
        data_crc(15 downto 8) when crc_2,
        tx_data_i             when others;

    tx_last <= tx_last_i;
    
    user_next <= tx_next when state=transmit else '0';
    
end gideon;
