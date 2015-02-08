-------------------------------------------------------------------------------
-- Title      : host_sequencer
-- Author     : Gideon Zweijtzer
-------------------------------------------------------------------------------
-- Description: This block generates the traffic on the downstream USB port.
--              This block has knowledge about the speeds, the USB frames,
--              and generates traffic within the right time within the frame.
--              The data interface of this block is a BRAM interface. This
--              block implements the three-time retry as well.
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.usb_pkg.all;
use work.usb_cmd_pkg.all;

entity host_sequencer is
generic (
    g_buffer_depth_bits : natural := 11 ); -- 2K
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    -- Access to buffer memory
    buf_address     : out unsigned(g_buffer_depth_bits-1 downto 0);
    buf_en          : out std_logic;
    buf_we          : out std_logic;
    buf_rdata       : in  std_logic_vector(7 downto 0);
    buf_wdata       : out std_logic_vector(7 downto 0);
    
    -- mode selection
    sof_enable      : in  std_logic;
    speed           : in  std_logic_vector(1 downto 0);
        
    -- low level command interface
    usb_cmd_req     : in  t_usb_cmd_req;
    usb_cmd_resp    : out t_usb_cmd_resp;

    -- I/O to interface block
    usb_rx          : in  t_usb_rx;
    usb_tx_req      : out t_usb_tx_req;
    usb_tx_resp     : in  t_usb_tx_resp );

end entity;

architecture rtl of host_sequencer is
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

        type t_state is (idle, prefetch, transmit_msg, wait_tx_done);
        signal state : t_state;
        signal transmit_en      : std_logic := '0';
        signal tx_last_i        : std_logic;
        signal tx_data_valid    : std_logic;
        signal tx_send_packet   : std_logic;
        signal buffer_index     : unsigned(1 downto 0);

    begin
        buffer_index    <= usb_cmd_req.buffer_index;
        buf_address     <= buffer_index & buffer_addr_i(8 downto 0);
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
                        state <= wait_tx_done;
                    elsif tx_last_i = '1' and tx_data_valid = '1' then
                        state <= wait_tx_done;
                    end if;

                when wait_tx_done =>
                    if usb_tx_resp.busy = '0' then
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
    
    b_replay: block
        type t_state is (idle, wait_sof, wait_split_done, do_token, wait_token_done, do_data, wait_tx_done, wait_device_response, wait_handshake_done );
                         
        signal state            : t_state;
        signal timeout          : boolean;
        signal timer            : integer range 0 to 1023 := 0;
        signal cmd_done         : std_logic;

        signal frame_div        : integer range 0 to 8191;
        signal frame_cnt        : unsigned(13 downto 0) := (others => '0');
        signal do_sof           : std_logic;
        signal sof_guard        : std_logic := '0';
        
        signal start_split_active       : boolean;
        signal complete_split_active    : boolean;
    begin
        start_split_active <= (usb_cmd_req.do_split = '1') and (usb_cmd_req.split_sc = '0') and (speed = "10");
        complete_split_active <= (usb_cmd_req.do_split = '1') and (usb_cmd_req.split_sc = '1') and (speed = "10");

        process(clock)
            procedure start_timer(value : integer := 767) is
            begin
                timeout <= false;
                timer <= value;
            end procedure;

        begin
            if rising_edge(clock) then

                send_packet_cmd <= '0';
                
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

                cmd_done <= '0';
                
                case state is
                when idle =>
                    receive_en <= '0';
                    start_timer;
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
                                                
                    elsif sof_guard = '0' and usb_cmd_req.request = '1' and cmd_done = '0' then
                        -- default response
                        usb_cmd_resp.no_data <= '1';
                        usb_cmd_resp.data_length <= (others => '0');
    
                        usb_tx_req_i.split_token.e  <= '0';
                        usb_tx_req_i.split_token.et <= usb_cmd_req.split_et;
                        usb_tx_req_i.split_token.sc <= usb_cmd_req.split_sc;
                        usb_tx_req_i.split_token.s  <= usb_cmd_req.split_sp;
                        usb_tx_req_i.split_token.hub_address <= std_logic_vector(usb_cmd_req.split_hub_addr);
                        usb_tx_req_i.split_token.port_address <= "000" & std_logic_vector(usb_cmd_req.split_port_addr);

                        usb_tx_req_i.token.device_addr <= std_logic_vector(usb_cmd_req.device_addr);
                        usb_tx_req_i.token.endpoint_addr <= std_logic_vector(usb_cmd_req.endp_addr);

                        if usb_cmd_req.do_split = '1' then
                            usb_tx_req_i.pid <= c_pid_split;
                            usb_tx_req_i.send_split <= '1';
                            state <= wait_split_done;
                        else
                            state <= do_token;
                        end if;                        
                    end if;

                when wait_sof =>
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                        usb_tx_req_i.send_handsh <= '0';
                        usb_tx_req_i.send_split <= '0';
                        state <= idle;
                    end if;

                when wait_split_done =>
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                        usb_tx_req_i.send_handsh <= '0';
                        usb_tx_req_i.send_split <= '0';
                        state <= do_token;
                    end if;
                
                when do_token =>
                    usb_tx_req_i.send_token <= '1';
                    state <= wait_token_done;

                    case usb_cmd_req.command is
                    when setup =>
                        usb_tx_req_i.pid <= c_pid_setup;
                    when in_request =>
                        usb_tx_req_i.pid <= c_pid_in;
                    when out_data =>
                        usb_tx_req_i.pid <= c_pid_out;
                    when others =>
                        usb_tx_req_i.pid <= c_pid_ping;
                    end case;

                when wait_token_done =>
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                        usb_tx_req_i.send_handsh <= '0';
                        usb_tx_req_i.send_split <= '0';
                        state <= do_data;
                    end if;
                                    
                when do_data =>
                    case usb_cmd_req.command is
                    when setup | out_data =>
                        if usb_cmd_req.do_data = '1' then
                            send_packet_cmd <= '1';
                            tx_no_data <= usb_cmd_req.no_data;
                            tx_length <= usb_cmd_req.data_length;
                            if usb_cmd_req.togglebit='0' then
                                usb_tx_req_i.pid <= c_pid_data0;
                            else
                                usb_tx_req_i.pid <= c_pid_data1;
                            end if;
                            state <= wait_tx_done;
                        else
                            state <= wait_device_response;
                        end if;                        

                    when in_request =>
                        receive_en <= usb_cmd_req.do_data;
                        state <= wait_device_response;

                    when others =>
                        state <= wait_device_response;
                    end case;
                    
                when wait_tx_done =>
                    start_timer;
                    if data_transmission_done = '1' then
                        state <= wait_device_response;
                    end if;

                when wait_device_response =>
                    if usb_tx_resp.busy='1' or usb_rx.receiving='1' then
                        start_timer;
                    end if;
                    usb_tx_req_i.pid <= c_pid_ack; 
                    if usb_rx.valid_handsh = '1' then
                        usb_cmd_resp.result <= encode_result(usb_rx.pid);
                        cmd_done <= '1';
                        state <= idle;
                    elsif usb_rx.error='1' or timeout then
                        usb_cmd_resp.result <= res_error;
                        cmd_done <= '1';
                        state <= idle;
                    elsif valid_packet_received = '1' then -- woohoo!
                        usb_cmd_resp.result <= res_data;
                        usb_cmd_resp.no_data <= rx_no_data;
                        usb_cmd_resp.data_length <= rx_length;
                        usb_cmd_resp.togglebit <= data_toggle_received;

                        -- we send an ack to the device. Thank you!
                        if complete_split_active and usb_cmd_req.split_et(0)='1' then
                            cmd_done <= '1';
                            state <= idle;
                        else
                            usb_tx_req_i.send_handsh <= '1';
                            state <= wait_handshake_done;
                        end if;
                    end if;

                when wait_handshake_done =>
                    if usb_tx_resp.request_ack = '1' then
                        usb_tx_req_i.send_token <= '0';
                        usb_tx_req_i.send_handsh <= '0';
                        usb_tx_req_i.send_split <= '0';
                        cmd_done <= '1';
                        state <= idle;
                    end if;                    

                when others =>
                    null;
                end case;                
                if reset='1' then
                    state <= idle;
                    usb_tx_req_i.pid <= X"0";
                    usb_tx_req_i.send_token <= '0';
                    usb_tx_req_i.send_split <= '0';
                    usb_tx_req_i.send_handsh <= '0';
                    do_sof <= '0';
                end if;
            end if;
        end process;
        usb_cmd_resp.done <= cmd_done;
    end block;
    
end architecture;
