-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2014 TECHNOLUTION BV, GOUDA NL
-- | =======          I                   ==          I    =
-- |    I             I                    I          I
-- |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===
-- |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I
-- |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I
-- |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I
-- |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I
-- |                 +---------------------------------------------------+
-- +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|
--      |            |             ++++++++++++++++++++++++++++++++++++++|
--      +------------+                          +++++++++++++++++++++++++|
--                                                         ++++++++++++++|
--                                                                  +++++|
--
-------------------------------------------------------------------------------
-- Title      : down_replayer
-- Author     : Gideon Zweijtzer (gideon.zweijtzer@technolution.eu)
-------------------------------------------------------------------------------
-- Description: This block generates the traffic on the downstream USB port,
--              based on the events that are received. 
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.usb_pkg.all;
use work.usb_network_pkg.all;
use work.tl_vector_pkg.all;

entity down_replayer is
generic (
    g_buffer_depth_bits : natural := 13 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    -- Access to buffer memory
    buf_address     : out unsigned(g_buffer_depth_bits-1 downto 0);
    buf_en          : out std_logic;
    buf_we          : out std_logic;
    buf_rdata       : in  std_logic_vector(7 downto 0);
    buf_wdata       : out std_logic_vector(7 downto 0);
    
    -- I/O to event queues 
    rx_event        : in  t_protocol_event;
    rx_event_ttl    : in  unsigned(7 downto 0);
    rx_event_valid  : in  std_logic; -- status
    rx_event_pop    : out std_logic; -- pulse

    -- Events that are finished and sent back to the upstream
    tx_event        : out t_protocol_event;
    tx_event_valid  : out std_logic; -- pulse
    tx_event_pop    : in  std_logic;
    
    -- Unfinsihed events; those that may need to be tried again
    pp_event        : out t_protocol_event;
    pp_event_ttl    : out unsigned(7 downto 0);
    pp_event_valid  : out std_logic;
    pp_event_pop    : in  std_logic;

    -- Endpoint state ram interface
    endp_address    : out unsigned(10 downto 0);
    endp_rdata      : in  std_logic_vector(3 downto 0);
    endp_wdata      : out std_logic_vector(3 downto 0);
    endp_read       : out std_logic;
    endp_write      : out std_logic;

    -- Other things
    do_reset        : out std_logic;
    do_suspend      : out std_logic;
    do_unsuspend    : out std_logic;

    connected       : in  std_logic;
    operational     : in  std_logic;
    suspended       : in  std_logic;
    sof_enable      : in  std_logic;
    speed           : in  std_logic_vector(1 downto 0);
        
    -- I/O to interface block
    usb_rx          : in  t_usb_rx;
    usb_tx_req      : out t_usb_tx_req;
    usb_tx_resp     : in  t_usb_tx_resp );

end entity;

architecture rtl of down_replayer is
    -- length identifiers to ram address counters
    signal tx_length        : unsigned(9 downto 0);
    signal tx_no_data       : std_logic;
    signal receive_en       : std_logic := '0';
    signal rx_length        : unsigned(9 downto 0);
    signal rx_no_data       : std_logic;
    signal send_packet_cmd  : std_logic;
    signal usb_tx_req_i     : t_usb_tx_req;
    signal valid_packet_received    : std_logic;
    signal data_toggle_received     : std_logic := '0';
    signal data_transmission_done   : std_logic := '0';
begin
    b_bram_control: block
        signal buffer_addr_i    : unsigned(9 downto 0) := (others => '1'); -- was undefined

        type t_state is (idle, prefetch, transmit_msg);
        signal state : t_state;
        signal transmit_en      : std_logic := '0';
        signal tx_last_i        : std_logic;
        signal tx_data_valid    : std_logic;
        signal tx_send_packet   : std_logic;
        signal buffer_index     : unsigned(3 downto 0);

    begin
        buffer_index    <= rx_event.buffer_index;
        buf_address     <= not buffer_index(3) & buffer_index(2 downto 0) & buffer_addr_i(8 downto 0);
        buf_we          <= usb_rx.data_valid and receive_en;
        buf_wdata       <= usb_rx.data;
        buf_en          <= receive_en or transmit_en;
        transmit_en     <= '1' when (state = prefetch) or
                                    ((state = transmit_msg) and (usb_tx_resp.data_wait='0'))
                            else '0';
        tx_data_valid   <= '1' when (state = transmit_msg) and (usb_tx_resp.data_wait='0')
                            else '0';
        tx_last_i       <= '1' when (buffer_addr_i = tx_length) else '0';

        process(clock)
        begin
            if rising_edge(clock) then
                if usb_rx.data_start = '1' or send_packet_cmd = '1' then
                    buffer_addr_i <= (others => '0');
                    rx_no_data <= '1';
                elsif (receive_en = '1' and usb_rx.data_valid = '1') or
                     ((transmit_en = '1') and not (tx_last_i='1' and tx_data_valid='1')) then
                    buffer_addr_i <= buffer_addr_i + 1;
                    rx_no_data <= '0';
                end if;

                valid_packet_received <= '0';
                data_transmission_done <= '0';
                if usb_rx.valid_packet = '1' and receive_en='1' then
                    rx_length <= buffer_addr_i;
                    valid_packet_received <= '1';
                    data_toggle_received <= get_togglebit(usb_rx.pid);
                end if;

                case state is
                when idle =>
                    if send_packet_cmd = '1' then
                        state <= prefetch;
                    end if;

                when prefetch =>
                    tx_send_packet <= '1';
                    state <= transmit_msg;
                
                when transmit_msg =>
                    if tx_send_packet = '0' and tx_no_data = '1' then
                        data_transmission_done <= '1';
                        state <= idle;
                    elsif tx_last_i = '1' and tx_data_valid = '1' then
                        data_transmission_done <= '1';
                        state <= idle;
                    end if;

                when others =>
                    null;
                end case;

                if usb_tx_resp.request_ack = '1' then
                    tx_send_packet <= '0';
                end if;

                if reset='1' then
                    state <= idle;
                    buffer_addr_i <= (others => '0');
                    tx_send_packet <= '0';
                end if;        
            end if;
        end process;
        usb_tx_req_i.data        <= buf_rdata;
        usb_tx_req_i.data_valid  <= tx_data_valid;
        usb_tx_req_i.data_last   <= tx_last_i when transmit_en='1' else '0';
        usb_tx_req_i.send_packet <= tx_send_packet;
        usb_tx_req_i.no_data     <= tx_no_data;
    end block;

    usb_tx_req <= usb_tx_req_i;
    
--    type t_event_type is ( setup_data, out_data, data_ack,
--                           in_request, in_data, out_pending,
--                           device_ack, device_nak, device_stall, device_error,
--                           thanks,
--                           reset_request, reset_status,
--                           connection_change, suspend, remote_wakeup ); -- 16
--
--
    b_replay: block
        type t_state is (idle, data_out, wait_for_handshake, wait_for_data, data_ack,
                         data_out_pending, wait_for_handshake_pending, wait_sof,
                         check_sequence_nr, check_suspended_event, check_dead_event,
                         wait_send_split, wait_for_handshake_split_data, report_status);
                         
        signal state            : t_state;
        signal rx_event_pop_i   : std_logic; 
        signal timeout          : boolean;
        signal timer            : integer range 0 to 1023 := 0;

        signal frame_div    : integer range 0 to 8191;
        signal frame_cnt    : unsigned(13 downto 0) := (others => '0');
        signal do_sof       : std_logic;
        signal sof_guard    : std_logic := '0';
        signal data_sent    : boolean;
        signal split_sent   : std_logic;
        
        signal tx_event_valid_i         : std_logic;
        signal pp_event_valid_i         : std_logic;
        
        signal start_split_active       : boolean;
        signal complete_split_active    : boolean;
    begin
        rx_event_pop <= rx_event_pop_i;
        tx_event_valid <= tx_event_valid_i;
        pp_event_valid <= pp_event_valid_i;

        endp_read <= '1' when (tx_event_valid_i='0' or tx_event_pop='1')
                          and (pp_event_valid_i='0' or pp_event_pop='1')
                          and (rx_event_valid='1' and rx_event_pop_i='0')
                         else '0';
        endp_address <= rx_event.device_addr & rx_event.endp_addr; -- use only device address?? (everywhere in the design of course)

        start_split_active <= (rx_event.split_valid = '1') and (rx_event.split_token.sc = '0') and (speed = "10");
        complete_split_active <= (rx_event.split_valid = '1') and (rx_event.split_token.sc = '1') and (speed = "10");

        process(clock)
            procedure start_timer(value : integer := 767) is
            begin
                timeout <= false;
                timer <= value;
            end procedure;

            procedure update_sequence_nr is
            begin
                endp_write <= '1';
            end procedure;
            
            procedure retry(decrement_ttl : boolean := true) is
            begin
                pp_event <= rx_event;
                -- if the time to live is set to max, we keep trying forever (until we are reset)
                if rx_event_ttl = unsigned(to_signed(-1, rx_event_ttl'length)) or not decrement_ttl then
                    pp_event_ttl <= rx_event_ttl;
                    pp_event_valid_i <= '1';
                else
                    pp_event_ttl <= rx_event_ttl - 1;
                    -- if the time to live has expired, we do not retry
                    if rx_event_ttl /= 0 then
                        pp_event_valid_i <= '1';
                    else
                        pp_event_valid_i <= '0';
                        tx_event_valid_i <= '1';
                        tx_event.event_type <= device_error;
                        update_sequence_nr;
                    end if;
                end if;
            end procedure;
            
            procedure insert_csplit(retry_count : unsigned(rx_event_ttl'range)) is
            begin
                pp_event <= rx_event;
                pp_event.split_token.sc <= '1';
                pp_event_ttl <= retry_count;
                pp_event_valid_i <= '1';
                rx_event_pop_i <= '1';
            end procedure;
        begin
            if rising_edge(clock) then
                if tx_event_pop = '1' then
                    tx_event_valid_i <= '0';
                end if;
                if pp_event_pop = '1' then
                    pp_event_valid_i <= '0';
                end if;
                
                rx_event_pop_i <= '0';
                send_packet_cmd <= '0';
                do_reset <= '0';
                do_suspend <= '0';
                do_unsuspend <= '0';
                
                receive_en <= '0';
                endp_write <= '0';
                endp_wdata <= std_logic_vector(next_sequence_nr(rx_event.sequence_nr)); 
                
                if timer = 1 then
                    timeout <= true;
                end if;
                if timer /= 0 then
                    timer <= timer - 1;
                end if;

                if frame_div = 800 then -- the last ~10% is unused for new transactions (test)
                    sof_guard <= '1';
                end if;
                if frame_div = 0 then
                    frame_div <= 7499; -- microframes
                    do_sof <= sof_enable; --operational and not suspended;                
                    sof_guard <= '0';
                    frame_cnt <= frame_cnt + 1;
                else
                    frame_div <= frame_div - 1;
                end if;

                case state is
                when idle =>
                    split_sent <= '0';
                    
                    start_timer(127);
                    if do_sof='1' then
                        do_sof <= '0';
                        usb_tx_req_i.pid <= c_pid_sof;
                        usb_tx_req_i.token.device_addr <= std_logic_vector(frame_cnt(9 downto 3));
                        usb_tx_req_i.token.endpoint_addr <= std_logic_vector(frame_cnt(13 downto 10));

                        case speed is
                        when "00" => -- low speed
                            if frame_cnt(2 downto 0) = "000" then
                                usb_tx_req_i.send_handsh <= '1';
                                state <= wait_sof;
                            end if;
                        when "01" => -- full speed
                            if frame_cnt(2 downto 0) = "000" then
                                usb_tx_req_i.send_token <= '1';
                                state <= wait_sof;
                            end if;
                        when "10" => -- high speed
                            usb_tx_req_i.send_token <= '1';
                            state <= wait_sof;
                        when others =>
                            null;
                        end case;
                                                
                    elsif (tx_event_valid_i='0' or tx_event_pop='1') and (pp_event_valid_i='0' or pp_event_pop='1') and (sof_guard = '0') then
                        -- by default, the outgoing event is very much like the incoming event!
                        tx_event <= rx_event;
                        tx_event.no_data <= '1';
                        tx_event.data_length <= (others => '0');
    
                        -- by default, the thing we need to transmit is very much like the incoming event!
                        tx_length <= rx_event.data_length;
                        tx_no_data <= rx_event.no_data;
                        usb_tx_req_i.token.device_addr <= std_logic_vector(rx_event.device_addr);
                        usb_tx_req_i.token.endpoint_addr <= std_logic_vector(rx_event.endp_addr);
                        usb_tx_req_i.split_token <= rx_event.split_token;
    
                        start_timer;
                        data_sent <= false;
                                                
                        if rx_event_valid='1' and rx_event_pop_i='0' then
                            if operational = '0' then
                                state <= check_dead_event;
                            elsif suspended = '1' then
                                state <= check_suspended_event;
                            else
                                state <= check_sequence_nr;
                            end if;
                        end if;
                    end if;

                when report_status =>
                    tx_event.data_length <= (others => '0');
                    tx_event.data_length(1 downto 0) <= unsigned(speed);
                    tx_event.data_length(4) <= connected;
                    tx_event.data_length(5) <= operational;
                    tx_event.data_length(6) <= suspended;
                    tx_event_valid_i <= '1';
                    rx_event_pop_i <= '1';
                    state <= idle;
                    
                when check_dead_event =>
                    case rx_event.event_type is
                    when reset_request =>
                        do_reset <= '1';
                        rx_event_pop_i <= '1';
                        state <= idle;
                    when get_status =>
                        state <= report_status;
                    when others =>
                        rx_event_pop_i <= '1';
                        tx_event.event_type <= illegal;
                        tx_event_valid_i <= '1';
                        state <= idle;
                    end case;
                
                when check_suspended_event =>
                    case rx_event.event_type is
                    when reset_request =>
                        do_reset <= '1';
                        rx_event_pop_i <= '1';
                        state <= idle;
                    when suspend =>
                        do_unsuspend <= not rx_event.togglebit;
                        rx_event_pop_i <= '1';
                        state <= idle;
                    when get_status =>
                        state <= report_status;
                    when others =>
                        rx_event_pop_i <= '1';
                        do_unsuspend <= '1';
                        --tx_event.event_type <= illegal;
                        --tx_event_valid_i <= '1';
                        retry(decrement_ttl => false);
                        state <= idle;
                    end case;

                when check_sequence_nr =>                    
                    if rx_event.sequence_nr = X"0" or rx_event.sequence_nr = unsigned(endp_rdata) then
                        if rx_event.split_valid = '1' and speed = "10" and split_sent = '0' then
                            usb_tx_req_i.pid <= c_pid_split;
                            usb_tx_req_i.send_split <= '1'; -- the split token itself has already been set in the idle state.
                            state <= wait_send_split;
                        else
                            case rx_event.event_type is
                            when setup_data => -- send SETUP token, send data packet, wait for ack, return data_ack (always)
                                usb_tx_req_i.pid <= c_pid_setup;
                                usb_tx_req_i.send_token <= '1';
                                state <= data_out;
                            when out_data => -- send OUT token, send data packet, wait for ack, return data_ack when success. (if not, 
                                usb_tx_req_i.pid <= c_pid_out;
                                usb_tx_req_i.send_token <= '1';
                                state <= data_out;
                            when in_request => -- send IN token, wait for data, return data or handshake
                                usb_tx_req_i.pid <= c_pid_in;
                                usb_tx_req_i.send_token <= '1';
                                state <= wait_for_data;
                            when setup_pending => -- send SETUP token, send data packet, wait for handshake, return handshake
                                usb_tx_req_i.pid <= c_pid_setup;
                                usb_tx_req_i.send_token <= '1';
                                state <= data_out_pending;
                            when out_pending => -- send OUT token, send data packet, wait for handshake, return handshake
                                usb_tx_req_i.pid <= c_pid_out;
                                usb_tx_req_i.send_token <= '1';
                                state <= data_out_pending;
                            when ping => -- send PING token, wait for handshake and return handshake
                                usb_tx_req_i.pid <= c_pid_ping;
                                usb_tx_req_i.send_token <= '1';
                                state <= wait_for_handshake_pending;
                            when reset_request =>
                                do_reset <= '1';
                                rx_event_pop_i <= '1';
                                state <= idle;
                            when suspend =>
                                do_suspend <= rx_event.togglebit;
                                rx_event_pop_i <= '1';
                                state <= idle;
                            when get_status =>
                                state <= report_status;
                            when others =>
                                rx_event_pop_i <= '1'; -- drop!
                                state <= idle;
                                report "Dropped rx_event of type: " & t_event_type'image(rx_event.event_type)
                                    severity warning;
                            end case;
                        end if;
                    else -- wrong sequence number
                        rx_event_pop_i <= '1';
                        if not is_old_sequence_nr(rx_event.sequence_nr, unsigned(endp_rdata)) then
                            retry(decrement_ttl => false);
                        end if;
                        state <= idle;
                    end if;
                    
                when wait_send_split =>
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_split <= '0';
                        state <= check_sequence_nr;
                        split_sent <= '1';
                    end if;

                when wait_sof =>
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                        usb_tx_req_i.send_handsh <= '0';
                        state <= idle;
                    end if;
                    
                when data_out =>
                    data_sent <= true;
                    start_timer;
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                        if rx_event.togglebit='1' then
                            usb_tx_req_i.pid <= c_pid_data1;
                        else 
                            usb_tx_req_i.pid <= c_pid_data0;
                        end if;
                    elsif usb_tx_resp.busy = '0' and usb_tx_req_i.send_token = '0' then
                        state <= wait_for_handshake;
                        if start_split_active then
                            send_packet_cmd <= '1'; -- handled by other state machine
                        elsif complete_split_active then
                            null; -- no send_packet_cmd, because we already did that with the SSPLIT
                        else -- not under split
                            send_packet_cmd <= '1'; -- handled by other state machine
                        end if;

                    end if;
                
                when wait_for_handshake =>
                    tx_event.event_type <= data_ack;
                    if usb_tx_resp.busy='1' or usb_rx.receiving='1' then
                        start_timer;
                    end if;
                    if usb_rx.valid_handsh = '1' then
                        if usb_rx.pid = c_pid_ack then -- for setups, the only answer can be ack, as per spec.
                            if start_split_active then
                                tx_event_valid_i <= '1'; -- send data ack.
                                insert_csplit(X"EE");
                                state <= idle;
                            elsif complete_split_active then
                                rx_event_pop_i <= '1'; -- done
                                update_sequence_nr;
                                state <= idle;
                            else
                                tx_event_valid_i <= '1'; -- send data ack.
                                rx_event_pop_i <= '1'; -- done
                                update_sequence_nr;
                                state <= idle;
                            end if;
                        else
                            retry;
                            rx_event_pop_i <= '1';
                            state <= idle;
                        end if;
                    elsif timeout or usb_rx.error = '1' then
                        retry;
                        rx_event_pop_i <= '1';
                        state <= idle;
                    end if;                        

                when wait_for_data =>
                    receive_en <= '1';
                    tx_event.event_type <= in_data;

                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                        if start_split_active and 
                           rx_event.split_token.et(0)='1' then -- endp_type = interrupt or rx_event.endp_type = isochronous) then
                            insert_csplit(X"EE");
                            state <= idle;
                        end if;
                    end if;
                    if usb_tx_resp.busy='1' or usb_rx.receiving='1' then
                        start_timer;
                    end if;
                    if usb_rx.valid_handsh = '1' then
                        case usb_rx.pid is
                        when c_pid_nak =>
                            tx_event.event_type <= device_nak;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        when c_pid_stall =>
                            tx_event.event_type <= device_stall;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        when c_pid_ack =>
                            if start_split_active then
                                insert_csplit(X"EE");
                                state <= idle;
                            else                            
                                tx_event.event_type <= device_error;
                                tx_event_valid_i <= '1';
                                rx_event_pop_i <= '1'; -- done
                                update_sequence_nr;
                                state <= idle;
                            end if;
                        when c_pid_nyet =>
                            if complete_split_active then
                                if rx_event_ttl = 0 then -- we are going to retire. Let's nack
                                    tx_event.event_type <= device_nak;
                                    tx_event_valid_i <= '1';
                                    update_sequence_nr;
                                else
                                    retry;
                                end if;
                                rx_event_pop_i <= '1';
                                state <= idle;
                            else
                                tx_event.event_type <= device_error;
                                tx_event_valid_i <= '1';
                                rx_event_pop_i <= '1'; -- done
                                update_sequence_nr;
                                state <= idle;
                            end if;
                        when others =>
                            tx_event.event_type <= device_error;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        end case;
                    elsif usb_rx.error='1' or timeout then
                        retry;
                        rx_event_pop_i <= '1';
                        state <= idle;
                    elsif valid_packet_received = '1' then -- woohoo!
                        tx_event.event_type <= in_data;
                        tx_event.no_data <= rx_no_data;
                        tx_event.data_length <= rx_length;
                        tx_event.togglebit <= data_toggle_received;
                        tx_event_valid_i <= '1';
                        usb_tx_req_i.pid <= c_pid_ack; 

                        if complete_split_active and rx_event.split_token.et(0)='1' then
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        else
                            usb_tx_req_i.send_handsh <= '1';
                            state <= data_ack;
                        end if;
                    end if;

                when data_ack =>
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_handsh <= '0';
                        rx_event_pop_i <= '1'; -- done
                        update_sequence_nr;
                        state <= idle;
                    end if;                    

                when data_out_pending =>
                    data_sent <= true;
                    start_timer;
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                        if rx_event.togglebit='0' then
                            usb_tx_req_i.pid <= c_pid_data0;
                        else 
                            usb_tx_req_i.pid <= c_pid_data1;
                        end if;
                    elsif usb_tx_resp.busy = '0' then
                        if start_split_active then
                            state <= wait_for_handshake_split_data;
                            send_packet_cmd <= '1'; -- handled by other state machine
                        elsif complete_split_active then
                            state <= wait_for_handshake_pending;
                            -- no send_packet_cmd, because we already did that with the SSPLIT
                        else
                            state <= wait_for_handshake_pending;
                            send_packet_cmd <= '1'; -- handled by other state machine
                        end if;
                    end if;

                when wait_for_handshake_split_data =>
                    if usb_tx_resp.busy='1' or usb_rx.receiving='1' then
                        start_timer;
                    end if;
                    if rx_event.split_token.et(0)='1' then -- endp_type = interrupt or rx_event.endp_type = isochronous then
                        if data_transmission_done='1' then
                            insert_csplit(X"EE");
                            state <= idle;
                        end if;
                    elsif usb_rx.valid_handsh = '1' then
                        case usb_rx.pid is
                        when c_pid_ack =>
                            insert_csplit(X"FF"); -- forever!!
                            state <= idle;
                        when c_pid_nak =>
                            tx_event.event_type <= device_nak;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        when c_pid_stall =>
                            tx_event.event_type <= device_stall;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        when others =>
                            tx_event.event_type <= device_error;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        end case;
                    elsif usb_rx.error='1' or timeout then
                        retry;
                        rx_event_pop_i <= '1';
                        state <= idle;
                    end if;

                when wait_for_handshake_pending =>
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                    end if;
                    if usb_tx_resp.busy='1' or usb_rx.receiving='1' then
                        start_timer;
                    end if;
                    if usb_rx.valid_handsh = '1' then
                        case usb_rx.pid is
                        when c_pid_ack =>
                            if data_sent then
                                tx_event.event_type <= device_ack;
                            else
                                tx_event.event_type <= ping_ack;
                            end if;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        when c_pid_nyet =>
                            if complete_split_active then
                                if rx_event_ttl = 0 then -- we are going to retire. Let's nack
                                    tx_event.event_type <= device_nak;
                                    tx_event_valid_i <= '1';
                                    update_sequence_nr;
                                else            
                                    retry;
                                end if;
                                rx_event_pop_i <= '1';
                                state <= idle;
                            else
                                if data_sent then
                                    tx_event.event_type <= device_ack;
                                else
                                    tx_event.event_type <= ping_ack;
                                end if;
                                tx_event_valid_i <= '1';
                                rx_event_pop_i <= '1'; -- done
                                update_sequence_nr;
                                state <= idle;
                            end if;
                        when c_pid_nak =>
                            if data_sent then
                                tx_event.event_type <= device_nak;
                            else
                                tx_event.event_type <= ping_nak;
                            end if;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        when c_pid_stall =>
                            tx_event.event_type <= device_stall;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        when others =>
                            tx_event.event_type <= device_error;
                            tx_event_valid_i <= '1';
                            rx_event_pop_i <= '1'; -- done
                            update_sequence_nr;
                            state <= idle;
                        end case;
                    elsif usb_rx.error='1' or timeout then
                        retry;
                        rx_event_pop_i <= '1';
                        state <= idle;
                    end if;

                when others =>
                    null;
                end case;                
                if reset='1' then
                    tx_event_valid_i <= '0';
                    state <= idle;
                    pp_event_valid_i <= '0';
                    usb_tx_req_i.pid <= X"0";
                    usb_tx_req_i.send_token <= '0';
                    usb_tx_req_i.send_split <= '0';
                    usb_tx_req_i.send_handsh <= '0';
                    do_sof <= '0';
                    split_sent <= '0';
                end if;
            end if;
        end process;
    end block;
    
end architecture;
