library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;

entity ulpi_host is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    -- Descriptor RAM interface
    descr_addr  : out std_logic_vector(8 downto 0);
    descr_rdata : in  std_logic_vector(31 downto 0);
    descr_wdata : out std_logic_vector(31 downto 0);
    descr_en    : out std_logic;
    descr_we    : out std_logic;

    -- Buffer RAM interface
    buf_addr    : out std_logic_vector(10 downto 0);
    buf_rdata   : in  std_logic_vector(7 downto 0);
    buf_wdata   : out std_logic_vector(7 downto 0);
    buf_en      : out std_logic;
    buf_we      : out std_logic;

    -- Transmit Path Interface
    tx_busy     : in  std_logic;
    tx_ack      : in  std_logic;
    
    -- Interface to send tokens and handshakes
    send_token  : out std_logic;
    send_handsh : out std_logic;
    tx_pid      : out std_logic_vector(3 downto 0);
    tx_token    : out std_logic_vector(10 downto 0);
    
    -- Interface to send data packets
    send_data   : out std_logic;
    no_data     : out std_logic;
    user_data   : out std_logic_vector(7 downto 0);
    user_last   : out std_logic;
    user_valid  : out std_logic;
    user_next   : in  std_logic;
            
    -- Interface to bus initialization unit
    reset_done  : in  std_logic;
	sof_enable  : in  std_logic;
	scan_enable : in  std_logic := '1';
    speed       : in  std_logic_vector(1 downto 0);
    abort       : in  std_logic;
    
    -- Receive Path Interface
    rx_pid          : in  std_logic_vector(3 downto 0);
    rx_token        : in  std_logic_vector(10 downto 0);
    valid_token     : in  std_logic;
    valid_handsh    : in  std_logic;

    valid_packet    : in  std_logic;
    data_valid      : in  std_logic;
    data_start      : in  std_logic;
    data_out        : in  std_logic_vector(7 downto 0);

    rx_error        : in  std_logic );
        
end ulpi_host;

architecture functional of ulpi_host is
    signal frame_div    : integer range 0 to 65535;
    signal frame_cnt    : unsigned(13 downto 0) := (others => '0');
    signal do_sof       : std_logic;

    constant c_max_transaction      : integer := 31;
    constant c_max_pipe             : integer := 31;
    constant c_timeout_val          : integer := 7167;
    constant c_transaction_offset   : unsigned(8 downto 6) := "001";    

    signal transaction_pntr : integer range 0 to c_max_transaction;
    signal descr_addr_i     : unsigned(8 downto 0); -- could be temporarily pipe addr

    type t_state is (startup, idle, wait4start, scan_transactions, get_pipe,
                     handle_trans, setup_token, bulk_token, send_data_packet, get_status,
                     wait_for_ack, receive_data, send_ack, update_pipe, update_trans, do_ping );
    signal state        : t_state;
    signal substate     : integer range 0 to 7;
    signal trans_in     : t_transaction;
    signal pipe_in      : t_pipe;

    signal trans_cnt    : unsigned(10 downto 0);
    signal trans_len    : unsigned(10 downto 0);
    signal buf_addr_i   : unsigned(10 downto 0);
--    signal speed        : std_logic_vector(1 downto 0) := "11";
    signal no_data_i    : boolean;
    signal abort_reg    : std_logic;
    signal tx_put       : std_logic;
    signal tx_last      : std_logic;
	signal need_ping	: std_logic;
    signal fifo_data_in : std_logic_vector(7 downto 0);
    signal tx_almost_full   : std_logic;
    signal link_busy	: std_logic;
    signal timeout      : boolean;
    signal timeout_cnt  : integer range 0 to c_timeout_val;
    signal first_transfer   : boolean;
    signal terminate    : std_logic;
    
    attribute fsm_encoding : string;
    attribute fsm_encoding of state : signal is "sequential";
--    attribute keep : string;
--    attribute keep of timeout : signal is "true";

    signal debug_count  : integer range 0 to 1023 := 0;
    signal debug_error  : std_logic := '0';
    
begin
    descr_addr <= std_logic_vector(descr_addr_i);
    buf_addr   <= std_logic_vector(buf_addr_i);
    no_data    <= '1' when no_data_i else '0';
    buf_wdata  <= data_out; -- should be rx_data
    buf_we     <= '1' when (state = receive_data) and (data_valid = '1') else '0';
    
    p_protocol: process(clock)
        procedure next_transaction is
        begin
            if terminate='1' then
                terminate <= '0';
                state <= idle;
            elsif transaction_pntr = c_max_transaction then
                transaction_pntr <= 0;
                state <= idle; -- wait for next sof before rescan
            else
                transaction_pntr <= transaction_pntr + 1;
                substate <= 0;
                state <= scan_transactions;
            end if;
        end procedure;
        function min(a, b: unsigned) return unsigned is
        begin
            if a < b then
                return a;
            else
                return b;
            end if;
        end function;

        variable trans_temp : t_transaction;
        variable len        : unsigned(trans_temp.transfer_length'range);
    begin
        if rising_edge(clock) then
            descr_en <= '0';
            descr_we <= '0';
            tx_put   <= '0';
            
            if abort='1' then
                abort_reg <= '1';
            end if;
            
            -- default counter
            if substate /= 3 then
                substate <= substate + 1;
            end if;

            if timeout_cnt /= 0 then
                timeout_cnt <= timeout_cnt - 1;
                if timeout_cnt = 1 then
                    timeout <= false;--true;
                end if;
            end if;

            case state is
            when startup =>
                tx_pid <= c_pid_reserved;
                do_sof <= '0';
                frame_div <= 7499;

                if reset_done='1' then
                    state <= idle;
					if speed = "10" then
						need_ping <= '1';
					end if;
                end if;                    
            
            when idle =>
                abort_reg <= '0';
                if do_sof='1' then
                    do_sof <= '0';
                    tx_token <= std_logic_vector(frame_cnt(13 downto 3));
                    tx_pid   <= c_pid_sof;
                    if speed = "00" then
                        send_handsh <= '1';
                    else
                        send_token <= '1';
                    end if;
                    if speed(1)='1' then
                        frame_cnt <= frame_cnt + 1;
                    else
                        frame_cnt <= frame_cnt + 8;
                    end if;
                    state <= wait4start;
                end if;

            when wait4start =>
                if tx_ack='1' then
                    send_token  <= '0';
                    send_handsh <= '0';
                    send_data   <= '0'; -- redundant - will not come here
                    substate <= 0;
                    if scan_enable='1' then
                        state <= scan_transactions;
                    else
                        state <= idle;
                    end if;                        
                end if;
                
            when scan_transactions =>
                case substate is
                when 0 =>
                    descr_addr_i <= c_transaction_offset & to_unsigned(transaction_pntr,
                                        descr_addr_i'length-c_transaction_offset'length);
                    descr_en     <= '1';
                when 2 =>
                    trans_temp := data_to_t_transaction(descr_rdata);
                    trans_in   <= trans_temp;
                    substate   <= 0;
                    if trans_temp.state = busy then
                        state <= get_pipe;
                    else -- go for next, unless we are at the end of the list
                        next_transaction;
                    end if;
                when others =>
                    null;
                end case;
                
            when get_pipe =>
                case substate is
                when 0 =>
                    descr_addr_i <= (others => '0');
                    descr_addr_i(trans_in.pipe_pointer'range) <= trans_in.pipe_pointer;
                    descr_en <= '1';
                when 2 =>
                    pipe_in <= data_to_t_pipe(descr_rdata);
                    first_transfer <= true;
                    state <= handle_trans; ---
                when others =>
                    null;
                end case;
            
            when handle_trans => -- both pipe and transaction records are now valid
                abort_reg <= '0';
				substate <= 0;
                if do_sof='1' and link_busy='0' then
                    state <= idle;
                elsif pipe_in.state /= initialized then -- can we use the pipe?
                    trans_in.state <= error;
                    state <= update_trans;
                else -- yes we can
                    timeout     <= false;
                    timeout_cnt <= c_timeout_val;
					link_busy <= trans_in.link_to_next;

                    case trans_in.transaction_type is
                    when control =>
                        -- a control out sequence exists of a setup token
                        -- and then a data0 packet, which should be followed by
                        -- an ack from the device. The next phase of the transaction
                        -- could be either in or out, and defines whether it is a
                        -- control read or a control write. 
                        -- By choice, control transfers are implemented using
                        -- two transactions, which are executed in guaranteed
                        -- sequence.
                        -- In this way, each stage has its own buffer.
                        -- Note, the first pipe should be of type OUT, although it is not
                        -- checked.
                        tx_pid       <= c_pid_setup;
                        tx_token     <= pipe_in.device_endpoint & pipe_in.device_address;
                        send_token   <= '1';
                        state        <= setup_token;

                    when bulk | interrupt =>
                        tx_token    <= pipe_in.device_endpoint & pipe_in.device_address;
                        state       <= bulk_token;
                        send_token  <= '1';
                        timeout     <= false;
                        timeout_cnt <= c_timeout_val;

                        if pipe_in.direction = dir_in then
                            tx_pid   <= c_pid_in;
                        else
--							if need_ping='1' then
--								tx_pid <= c_pid_ping;
--								state  <= do_ping;
--							else								
	                            tx_pid   <= c_pid_out;
--	                        end if;
                        end if;
                        if pipe_in.control='1' and first_transfer then
                            pipe_in.data_toggle <= '1'; -- start with data 1
                        end if;
                        first_transfer <= false;
                                                
                    when others => -- not yet supported
                        trans_in.state <= error;
                        state <= update_trans;

                    end case;
                end if;
                    
            when setup_token =>
                if tx_ack='1' then
                    send_token <= '0';
                    tx_pid     <= c_pid_data0; -- send setup data immediately
                    send_data  <= '1';
                    buf_en     <= '1';
                    substate   <= 0;
                    state      <= send_data_packet;
                end if;
                -- prepare buffer
                buf_addr_i <= trans_in.buffer_address;
                trans_len  <= trans_in.transfer_length; -- not cut up
                trans_cnt  <= trans_in.transfer_length; -- not cut up
                no_data_i  <= (trans_in.transfer_length = 0); 

			when do_ping =>
				if tx_ack='1' then
					send_token <= '0';
				end if;
				-- wait for ack/nack or nyet.
                if rx_error='1' then
                    trans_in.state <= error;
                    state <= update_trans;
                elsif abort_reg='1' then
                    pipe_in.state <= aborted;
                    state <= update_pipe;
                    abort_reg <= '0';
                elsif valid_handsh='1' then -- maybe an ack?
                    if rx_pid = c_pid_ack then
                        tx_pid     <= c_pid_out;
						send_token <= '1';
						state      <= bulk_token;
                    elsif rx_pid = c_pid_stall then
                        pipe_in.state <= stalled;
                        trans_in.state <= error;
                        state <= update_pipe;
                    elsif (rx_pid = c_pid_nak) or (rx_pid = c_pid_nyet) then
                        state <= handle_trans;
                    end if; -- all other pids are just ignored
				elsif timeout then
                    state <= handle_trans;
                end if;                    

            when bulk_token =>
                if tx_ack='1' then
                    send_token <= '0';
                    if pipe_in.direction = dir_out then
                        if pipe_in.data_toggle = '0' then
                            tx_pid <= c_pid_data0;
                        else
                            tx_pid <= c_pid_data1;
                        end if;
                        send_data  <= '1';
                        buf_en     <= '1';
                        substate   <= 0;
                        state      <= send_data_packet;
                    else -- input
                        timeout     <= false;
                        timeout_cnt <= c_timeout_val;
                        state       <= receive_data;
                        buf_en      <= '1';
                    end if;
                end if;
                -- prepare buffer
                buf_addr_i <= trans_in.buffer_address;
                if pipe_in.direction = dir_out then
                    len := min(trans_in.transfer_length, pipe_in.max_transfer);
                    trans_len  <= len; -- possibly cut up
                    trans_cnt  <= len;
                    no_data_i  <= (trans_in.transfer_length = 0); 
                else
                    trans_len  <= (others => '0');
                end if;

            when send_data_packet =>
                case substate is
                when 0 =>
                    if tx_ack='1' then
                        send_data <= '0';
                        if no_data_i then
                            substate <= 2;
                        end if;
                    else
                        substate <= 0;
                    end if;
                when 1 =>
                    substate <= 1; -- stay!
                    if tx_almost_full='0' then
                        tx_put <= '1';
                        buf_addr_i <= buf_addr_i + 1;
                        trans_cnt <= trans_cnt - 1;
                        if trans_cnt = 1 then
                            tx_last <= '1';
                            substate <= 2;
                            buf_en <= '0';
                        else
                            tx_last <= '0';
                        end if;
                    end if;
                    
                when 2 =>
                    if tx_busy='1' then
                        substate <= 2;
                    else
                        state       <= wait_for_ack;
                        timeout     <= false;
                        timeout_cnt <= c_timeout_val;
                    end if;
                    
                when others =>
                    null;
                end case;
            
            when wait_for_ack =>
                if rx_error='1' then
                    trans_in.state <= error;
                    state <= update_trans;
                elsif abort_reg='1' then
                    pipe_in.state <= aborted;
                    state <= update_pipe;
                    abort_reg <= '0';
                elsif valid_handsh='1' then -- maybe an ack?
                    if (rx_pid = c_pid_ack) or (rx_pid = c_pid_nyet) then
						if rx_pid = c_pid_nyet then
							need_ping <= '1';
						else
							need_ping <= '0';
						end if;
                        if trans_in.transfer_length = trans_len then
                            trans_in.state <= done;
                            if pipe_in.control='1' and trans_in.transaction_type = bulk then
                                state <= get_status;
                                substate <= 0;
                            else
                                state <= update_pipe;
                            end if;
                        else
                            trans_in.state <= busy;
                            state <= handle_trans;
                        end if;
                        trans_in.buffer_address    <= buf_addr_i; -- store back
                        trans_in.transfer_length   <= trans_in.transfer_length - trans_len;
                        pipe_in.data_toggle        <= not pipe_in.data_toggle;

                    elsif rx_pid = c_pid_stall then
                        pipe_in.state <= stalled;
                        trans_in.state <= error;
                        state <= update_pipe;
                    elsif rx_pid = c_pid_nak then
                        terminate <= '0'; --link_busy; -- if control packet, then don't continue with next transaction!
                        state <= update_trans;
--                        state <= handle_trans; -- just retry and retry, no matter what kind of packet it is, don't send SOF!
                    end if; -- all other pids are just ignored
--				elsif do_sof='1' then
--					state <= idle; -- test
				elsif timeout then
                    pipe_in.timeout <= '1';
                    trans_in.state <= error;
                    state <= update_pipe;
--                    state <= handle_trans; -- try again
                end if;

            when get_status =>
                case substate is
                when 0 =>
                    send_token <= '1';
                    tx_pid <= c_pid_in;
                when 1 =>
                    if tx_ack='1' then
                        send_token <= '0';
                        timeout_cnt <= c_timeout_val;
                        timeout <= false;
                    else
                        substate <= 1; -- wait
                    end if;
                when 2 =>
                    if valid_packet='1' or valid_handsh='1' then
                        state <= update_pipe; -- end transaction
                    elsif rx_error='1' or timeout then
                        trans_in.state <= error;
                        state <= update_pipe; -- end transaction
                    else                        
                        substate <= 2; -- wait
                    end if;
                when others =>
                    null;
                end case;
                
            when receive_data =>
                if data_valid = '1' then
                    timeout <= false;
                    timeout_cnt <= 0; -- does not occur anymore
                    buf_addr_i <= buf_addr_i + 1;
                    trans_len <= trans_len + 1;
                end if;
--------------------------------------------------------------------
                if rx_error = '1' or debug_error='1' then
                    -- go back to send the in token again
                    buf_en <= '0';
                    state <= handle_trans;
                elsif abort_reg='1' then
                    pipe_in.state <= aborted;
                    state <= update_pipe;
                    abort_reg <= '0';
                elsif valid_packet='1' then
                    buf_en <= '0';
                    trans_in.buffer_address    <= buf_addr_i - 2; -- cut off CRC
                    trans_in.transfer_length   <= trans_in.transfer_length - (trans_len - 2);
                    if ((trans_len - 2) >= trans_in.transfer_length) or 
                       ((trans_len - 2) < pipe_in.max_transfer) then
                        trans_in.state <= done;
                    else
                        trans_in.state <= busy;
                    end if;
                    state <= send_ack;
                    substate <= 0;
                elsif valid_handsh='1' then
                    buf_en <= '0';
                    if rx_pid = c_pid_nak then
						if pipe_in.control='1' then
                            state <= idle; -- retry on next sof, do not go to the next transaction
                        else
                            state <= update_trans; -- is not updated, but is the standard path to go to the next transact.
                        end if;
                    elsif rx_pid = c_pid_stall then
                        trans_in.state <= error;
                        pipe_in.state <= stalled;
                        state <= update_pipe;
                    end if;
                elsif timeout then -- device doesn't answer, could it have missed my in token?
                    buf_en <= '0';
                    state <= handle_trans;
                end if;
                    
            when send_ack =>
                case substate is
                when 0 =>
                    send_handsh <= '1';
                    tx_pid <= c_pid_ack;
                when 1 =>
                    if tx_ack='0' then
                        substate <= 1; -- stay here.
                    else
                        send_handsh <= '0';
                        state <= update_trans;

--						if (pipe_in.control='0') and (trans_in.state = done) then
--                            state <= update_trans;
--                        elsif (pipe_in.control='1') and (trans_len = 2) then -- no data, thus status already received
--                            state <= update_trans;
--                        else
--                            null;
--                            -- substate <= 2;
--                        end if;
                    end if;
--                when 2 => -- send status back (no data packet)
--                    tx_pid     <= c_pid_out;
--                    tx_token   <= pipe_in.device_endpoint & pipe_in.device_address;
--                    send_token <= '1';
--                when 3 => -- wait until token was sent
--                    if tx_ack='0' then
--                        substate <= 3;
--                    else
--                        send_token <= '0';
--                        no_data_i <= true;
--                        send_data <= '1';
--                        tx_pid <= c_pid_data1;
--                    end if;
--                when 4 => -- wait until no data packet was processed
--                    if tx_ack='0' then
--                        substate <= 4;
--                    else
--                        send_data <= '0';
--                        state <= update_trans;
--                    end if;
                    
                when others =>
                    null;
                end case;

            when update_pipe =>
                descr_addr_i <= (others => '0');
                descr_addr_i(trans_in.pipe_pointer'range) <= trans_in.pipe_pointer;
                descr_en     <= '1';
                descr_we     <= '1';
                descr_wdata  <= t_pipe_to_data(pipe_in);
                state <= update_trans;
                
            when update_trans =>
                descr_addr_i <= c_transaction_offset & to_unsigned(transaction_pntr,
                                    descr_addr_i'length-c_transaction_offset'length);
                descr_wdata  <= t_transaction_to_data(trans_in);
                descr_en     <= '1';
                descr_we     <= '1';
                next_transaction;
                                    
            when others => 
                null;
            end case;

---------------------------------------------------
--          DEBUG
---------------------------------------------------
--            if state /= receive_data then
--                debug_count <= 0;
--                debug_error <= '0';
--            elsif debug_count = 1023 then
--                debug_error <= '1';
--            else
--                debug_count <= debug_count + 1;
--            end if;
---------------------------------------------------

            if frame_div = 0 then
                do_sof <= sof_enable;
                if speed(1)='1' then
                    frame_div <= 7499; -- microframes
                else
                    frame_div <= 59999; -- 1 ms frames
                end if;
            else
                frame_div <= frame_div - 1;
            end if;

            if reset_done='0' then
                state <= startup;
            end if;
    
			if speed /= "10" then -- If not high speed, then we force no ping
				need_ping <= '0';
			end if;

            if reset = '1' then
                abort_reg    <= '0';
                buf_en       <= '0';
                buf_addr_i   <= (others => '0');
                trans_len    <= (others => '0');
                trans_cnt    <= (others => '0');
				link_busy    <= '0';
                state        <= startup;
                do_sof       <= '0';
                frame_div    <= 7499;
                frame_cnt    <= (others => '0');                
                send_token   <= '0';
                send_data    <= '0';
                send_handsh  <= '0';
                need_ping	 <= '0';
                terminate    <= '0';
            end if;                
        end if;
    end process;

    -- Decoupling of ulpi tx bus and our generation of data
    -- to meet timing of "next" signal
--    fifo_data_in <= reset_data when (state = startup) else buf_rdata;
    fifo_data_in <= buf_rdata;

    i_srl_tx: entity work.srl_fifo
    generic map (
        Width      => 9,
        Depth      => 15,
        Threshold  => 10 ) 
    port map (
        clock               => clock,
        reset               => reset,
        GetElement          => user_next,
        PutElement          => tx_put,
        FlushFifo           => '0',
        DataIn(7 downto 0)  => fifo_data_in,
        DataIn(8)           => tx_last,
        DataOut(7 downto 0) => user_data,
        DataOut(8)          => user_last,
        SpaceInFifo         => open,
        AlmostFull          => tx_almost_full,
        DataInFifo          => user_valid );
    
end functional;
