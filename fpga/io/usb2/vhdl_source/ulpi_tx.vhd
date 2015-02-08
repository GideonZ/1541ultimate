library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;

entity ulpi_tx is
generic (
    g_simulation      : boolean := false;
    g_support_split   : boolean := true;
    g_support_token   : boolean := true );
port (
    clock       : in    std_logic;
    reset       : in    std_logic;
    
    -- Bus Interface
    tx_start    : out   std_logic;
    tx_last     : out   std_logic;
    tx_valid    : out   std_logic;
    tx_next     : in    std_logic;
    tx_data     : out   std_logic_vector(7 downto 0);
    
    -- CRC calculator
    crc_sync        : out std_logic;
    crc_dvalid      : out std_logic;
    data_to_crc     : out std_logic_vector(7 downto 0);
    data_crc        : in  std_logic_vector(15 downto 0);

    -- Status
    status      : in    std_logic_vector(7 downto 0);
    speed       : in    std_logic_vector(1 downto 0);

    usb_tx_req  : in    t_usb_tx_req;
    usb_tx_resp : out   t_usb_tx_resp );

end ulpi_tx;


architecture gideon of ulpi_tx is
    type t_state is (idle, crc_1, crc_2, token0, token1, token2, token3,
                     transmit, wait4next, write_end, handshake, gap, gap2);
    signal state    : t_state;

    signal tx_data_i    : std_logic_vector(7 downto 0);
    signal tx_last_i    : std_logic;
    signal token_crc    : std_logic_vector(4 downto 0) := "00000";
    signal split_crc    : std_logic_vector(4 downto 0) := "00000";
    signal no_data_d    : std_logic;
    signal gap_count    : integer range 0 to 2047;

    signal rd_data      : std_logic_vector(7 downto 0);
    signal rd_last      : std_logic;
    signal rd_next      : std_logic;
    signal token_vector : std_logic_vector(18 downto 0);
    signal long         : boolean;
    signal fifo_flush   : std_logic;
    signal busy         : std_logic;
    
    -- internal fifo is 3 bytes as it seems. 3 bytes is at max 40 bits incl. 1.5 SE0 EOP. at Full speed this is 40*5 = 200 clocks
    -- at low speed this is 40*40 clocks = 1600 
    type t_int_array is array (natural range <>) of integer;
    constant c_gap_values : t_int_array(0 to 3) := ( 1599, 199, 15, 15 ); 

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
    usb_tx_resp.request_ack <= (usb_tx_req.send_token or usb_tx_req.send_handsh or usb_tx_req.send_packet or usb_tx_req.send_split)
                                  when (state = idle) else '0';
    
    usb_tx_resp.busy        <= busy;

    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                tx_start  <= '0';
                tx_valid  <= '0';
                tx_last_i <= '0';
                fifo_flush <= '0';
                tx_data_i <= X"00";
                no_data_d <= usb_tx_req.no_data;
                long <= false;

                if usb_tx_req.send_token='1' and g_support_token then
                    token_vector <= token_to_vector(usb_tx_req.token) & X"00";
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data_i <= X"4" & usb_tx_req.pid;
                    state <= token1;
                elsif usb_tx_req.send_split='1' and g_support_split then
                    token_vector <= split_token_to_vector(usb_tx_req.split_token);
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data_i <= X"4" & usb_tx_req.pid;
                    long <= true;
                    state <= token0;
                elsif usb_tx_req.send_handsh='1' then
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data_i <= X"4" & usb_tx_req.pid;
                    tx_last_i <= '1';
                    state <= handshake;
                elsif usb_tx_req.send_packet='1' then
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data_i <= X"4" & usb_tx_req.pid;
                    state <= wait4next;
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
                    fifo_flush <= '1';
                    state <= crc_2;
                end if;
            
            when crc_2 =>
                if tx_next = '1' then
                    tx_last_i <= '0';
                    tx_valid  <= '0';
                    state <= gap;
                end if;
                
            when token0 =>
                if tx_next = '1' then
                    tx_start  <= '0';
                    tx_data_i <= token_vector(7 downto 0);
                    state     <= token1;
                end if;

            when token1 =>
                if tx_next = '1' then
                    tx_start  <= '0';
                    tx_data_i <= token_vector(15 downto 8);
                    state     <= token2;
                end if;
            
            when token2 =>
                if tx_next = '1' then
                    if long then
                        tx_data_i <= split_crc & token_vector(18 downto 16);
                    else
                        tx_data_i <= token_crc & token_vector(18 downto 16);
                    end if;
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
                if g_simulation then
                    gap_count <= 15;
                else
                    gap_count <= c_gap_values(to_integer(unsigned(speed)));
                end if;
                state <= gap2;
                
            when gap2 => -- TODO: look for squelch in high speed
                if status(4)='1' then
                    state <= idle;
                elsif gap_count = 0 then
                    state <= idle;
                else
                    gap_count <= gap_count - 1;
                end if;

            when transmit =>
                if tx_next='1' and rd_last='1' then
                    state <= crc_1;
                end if;

            when others => 
                null;
            end case;                                    

            if reset='1' then
                state  <= idle;
                fifo_flush <= '0';
            end if;
        end if;
    end process;

    crc_dvalid   <= '1' when (state = transmit) and tx_next='1' else '0';
    --crc_sync     <= '1' when (state = idle) else '0';
    crc_sync     <= usb_tx_req.send_packet;
    busy         <= '0' when (state = idle) else '1'; -- or (state = gap) else '1';
        
    g_token: if g_support_token generate
        i_token_crc: entity work.token_crc
        port map (
            clock       => clock,
            token_in    => token_vector(18 downto 8),
            crc         => token_crc );
    end generate;
    
    g_split: if g_support_split generate
        i_split_crc: entity work.token_crc_19
        port map (
            clock       => clock,
            token_in    => token_vector(18 downto 0),
            crc         => split_crc );

    end generate;
    
    with state select tx_data <=
        rd_data               when transmit,
        data_crc(7 downto 0)  when crc_1,
        data_crc(15 downto 8) when crc_2,
        tx_data_i             when others;

    tx_last <= tx_last_i;
    rd_next <= '1' when (tx_next = '1') and (state = transmit) else '0';
    
    i_tx_fifo: entity work.srl_fifo
    generic map (
        Width       => 9,
        Threshold   => 13 )
    port map (
        clock               => clock,
        reset               => reset,
        GetElement          => rd_next,
        PutElement          => usb_tx_req.data_valid,
        FlushFifo           => fifo_flush,
        DataIn(8)           => usb_tx_req.data_last,
        DataIn(7 downto 0)  => usb_tx_req.data,
        DataOut(8)          => rd_last,
        DataOut(7 downto 0) => rd_data,
        SpaceInFifo         => open,
        AlmostFull          => usb_tx_resp.data_wait,
        DataInFifo          => open );
    
    data_to_crc <= rd_data;
    
end gideon;
