library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;


entity bus_reset is
generic (
    g_simulation    : boolean := false );
port (
    clock       : in    std_logic;
    reset       : in    std_logic;
    
    reset_done  : out   std_logic;
	sof_enable  : out   std_logic;
	scan_enable : out   std_logic;
    speed       : out   std_logic_vector(1 downto 0);
    abort       : out   std_logic;    
    -- status
    status      : in    std_logic_vector(7 downto 0);
	usb_busy	: out   std_logic;
    
    -- other controls
    gap_length  : out   std_logic_vector(7 downto 0);
	
    -- command response interface
    cmd_empty   : in    std_logic;
    cmd_data    : in    std_logic_vector(7 downto 0);
    cmd_get     : out   std_logic;

    resp_full   : in    std_logic;
    resp_put    : out   std_logic;
    resp_data   : out   std_logic_vector(8 downto 0);

    -- register interface
    reg_read    : out   std_logic;
    reg_write   : out   std_logic;
    reg_rdata   : in    std_logic_vector(7 downto 0);
    reg_wdata   : out   std_logic_vector(7 downto 0);
    reg_address : out   std_logic_vector(5 downto 0);
    reg_ack     : in    std_logic;
    
    send_packet : out   std_logic;
    user_data   : out   std_logic_vector(7 downto 0);
    user_last   : out   std_logic;
    user_valid  : out   std_logic );

end bus_reset;

architecture functional of bus_reset is
    type t_state is (idle, start_reset, set_se0, listen_chirp,
                     wait_chirp_end, setup_chirp, hub_chirp_k, hub_chirp_j,
                     reset_end, reset_finished,
                     user_reg_read, user_reg_write, user_write_2, send_resp );

    type t_int_bool_array is array(boolean) of integer;
    constant c_reset_times   : t_int_bool_array := (false => 60000*15, true => 2097); -- 4194303 
    constant c_latest_chirp  : t_int_bool_array := (false => 80000, true => 400);
    constant c_stop_chirp    : t_int_bool_array := (false => 20000, true => 100);
    constant c_chirp_jk      : t_int_bool_array := (false => 3000, true => 20);
    constant c_filter_times  : t_int_bool_array := (false => 255, true => 10);

    signal state        : t_state;                    
    signal speed_i      : std_logic_vector(1 downto 0);
    signal low_speed    : std_logic;
    signal disable_hs   : std_logic;
    signal t0_expired   : std_logic;
    signal t2_expired   : std_logic := '0';
    signal timer_0      : integer range 0 to 4194303; -- ~  70 ms
    signal timer_1      : integer range 0 to 8191;    -- ~ 136 us
    signal timer_2      : integer range 0 to 31 := 31; -- 500 ns
    signal stop_chirp   : std_logic;
    signal reset_done_i : std_logic;
    signal latest_chirp_start : std_logic;
    signal cmd_valid    : std_logic;
    signal cmd_get_i    : std_logic;

    signal debug        : std_logic;
    
--    attribute fsm_encoding : string;
--    attribute fsm_encoding of state : signal is "sequential";
begin
    speed      <= speed_i;
    reset_done <= reset_done_i;
    cmd_get    <= cmd_get_i;
    
    cmd_get_i <= '1' when cmd_empty='0' and cmd_valid='0' and (state=idle or state=user_reg_write)
            else '0';

    p_reset: process(clock)
    begin
        if rising_edge(clock) then
            if timer_0 = 0 then
                t0_expired <= '1';
            else
                timer_0 <= timer_0 - 1;
            end if;
            if timer_0 = c_stop_chirp(g_simulation) then
                stop_chirp <= '1';
            end if;
            if timer_0 = c_latest_chirp(g_simulation) then
                latest_chirp_start <= '1';
            end if;
            if timer_2 = 0 then
                t2_expired <= '1';
            else
                timer_2 <= timer_2 - 1;
            end if;
            
            cmd_valid <= cmd_get_i;
            resp_put  <= '0';
            abort <= '0';
            
            case state is
            when idle =>
                reg_address <= cmd_data(5 downto 0);

                if cmd_valid = '1' then
                    case cmd_data(7 downto 6) is
                    when "00" =>
                        debug <= '0';
                        case cmd_data(3 downto 0) is
                        when c_cmd_get_status =>
                            resp_data <= "0" & status;
                            state <= send_resp;
                        when c_cmd_get_done =>
                            resp_data <= X"00" & reset_done_i;
                            state <= send_resp;
                        when c_cmd_get_speed =>
                            resp_data <= "0000000" & speed_i;
                            state <= send_resp;
                        when c_cmd_do_reset_hs =>
                            disable_hs <= '0';
                            state <= start_reset;
                        when c_cmd_do_reset_fs =>
                            disable_hs <= '1';
                            state <= start_reset;
                        when c_cmd_disable_host =>
                            reset_done_i <= '0';
                        when c_cmd_abort =>
                            abort <= '1';
					    when c_cmd_sof_enable =>
							sof_enable <= '1';
					    when c_cmd_sof_disable =>
							sof_enable <= '0';
						when c_cmd_set_busy =>
							usb_busy <= '1';
						when c_cmd_clear_busy =>
							usb_busy <= '0';
                        when c_cmd_disable_scan =>
                            scan_enable <= '0';
                        when c_cmd_enable_scan =>
                            scan_enable <= '1';
                        when c_cmd_set_debug =>
                            debug <= '1';
                        when others =>
                            if debug='1' then
                                resp_data <= '0' & X"AB";
                            else
                                resp_data <= '0' & X"AA";
                            end if;                            
                            state <= send_resp;
                        end case;
--                    when "01" =>
--                        gap_length <= "00" & cmd_data(5 downto 0);
                    when "11" =>
                        state <= user_reg_write;
                    when "10" =>
                        reg_read <= '1';
                        state <= user_reg_read;
                    when others =>
                        null;
                    end case;
                end if;

            when user_reg_read =>
                if reg_ack = '1' then
                    reg_read <= '0';
                    resp_data <= "1" & reg_rdata;
                    state <= send_resp;
                end if;
            
            when user_reg_write =>
                if cmd_valid = '1' then
                    reg_wdata <= cmd_data;
                    reg_write <= '1';
                    state <= user_write_2;
                end if;
            
            when user_write_2 =>
                if reg_ack = '1' then
                    reg_write <= '0';
                    state <= idle;
                end if;
                
            when send_resp =>
                if resp_full = '0' then
                    resp_put <= '1';
                    state <= idle;
                end if;

            when start_reset =>
                timer_0 <= c_reset_times(g_simulation);
                latest_chirp_start <= '0';
                t0_expired   <= '0';
                stop_chirp   <= '0';
                reset_done_i <= '0';
                low_speed    <= '0';

                if status(5 downto 2) /= "0011" then
                    speed_i <= "11"; -- not powered or rx active
                    state <= idle;
                else
                    if status(1)='1' then
                        low_speed <= '1';
                        speed_i <= "00"; -- Low speed
                    else
                        speed_i <= "01"; -- assume FS
                    end if;
                    state <= set_se0;
                end if;

            when set_se0 =>
                reg_address    <= std_logic_vector(to_unsigned(4, reg_address'length));
                reg_write  <= '1';
                reg_wdata <= X"50";
                timer_1 <= c_filter_times(g_simulation); -- reset timer 1 (4.25 �s)
                if reg_ack = '1' then
                    reg_write <= '0';
                    if low_speed='1' or disable_hs='1' then
                        state <= reset_end;
                    else
                        state <= listen_chirp;
                    end if;
                end if;

            when listen_chirp =>
                if t0_expired='1' then
                    state <= reset_end; -- no chirp detected
                elsif status(1)='0' then
                    timer_1 <= c_filter_times(g_simulation); -- reset timer
                elsif timer_1 = 0 then -- chirp detected
                    speed_i <= "10"; -- HS!
                    state <= setup_chirp; -- Let's be RUDE and just send our chirp back -- wait_chirp_end;
                    timer_1 <= 2 * c_chirp_jk(g_simulation);
                else
                    timer_1 <= timer_1 - 1;
                end if;

            when wait_chirp_end =>
                if t0_expired='1' then
                    speed_i <= "11"; -- error
                    state <= reset_end;
                elsif status(1)='0' then
                    if timer_1 = 0 then
                        if latest_chirp_start = '1' then
                            speed_i <= "11";
                            state <= reset_end;
                        else
                            state <= setup_chirp;
                        end if;
                    else
                        timer_1 <= timer_1 - 1;
                    end if;
                else
                    timer_1 <= 2 * c_chirp_jk(g_simulation); -- reset timer
                end if;

            when setup_chirp =>
                timer_1 <= c_chirp_jk(g_simulation);
                send_packet <= '1';
                state <= hub_chirp_k;

            when hub_chirp_k =>
                user_data <= X"00";
                user_valid <= '1';
                user_last  <= '0';
                send_packet <= '0';

                if timer_1 = 0 then
                    if stop_chirp = '1' then
                        state <= reset_end;
                        user_last <= '1'; -- data is still 0
                    else
                        user_data <= X"FF";
                        state <= hub_chirp_j;
                        timer_1 <= c_chirp_jk(g_simulation);
                    end if;
                else
                    timer_1 <= timer_1 - 1;
                end if;

            when hub_chirp_j =>
                if timer_1 = 0 then
                    timer_1 <= c_chirp_jk(g_simulation);
                    user_data <= X"00";
                    state <= hub_chirp_k;
                else
                    timer_1 <= timer_1 - 1;
                end if;

            when reset_end =>
                user_valid <= '0';
                user_last  <= '0';
                if t0_expired = '1' then
                    reg_address    <= std_logic_vector(to_unsigned(4, reg_address'length));
                    reg_write  <= '1';
                    reg_wdata <= map_speed(speed_i) or X"20"; -- reset bit set
                    state <= reset_finished;
               end if;
            
            when reset_finished =>
                if reg_ack='1' then
                    reg_write <= '0';
                    reset_done_i <= '1';
                    state <= idle;
                end if;
            
            when others =>
                null;
            end case;

            if reset = '1' then
                disable_hs   <= '0';
                speed_i      <= "11"; -- error or uninitialized
                state        <= idle;
                reset_done_i <= '0';
				sof_enable   <= '0';
                scan_enable  <= '1';
                user_data    <= X"00";
                user_last    <= '0';
                user_valid   <= '0';
                send_packet  <= '0';
                reg_read     <= '0';
                reg_write    <= '0';
                reg_wdata    <= X"00";
                reg_address  <= (others => '0');
                resp_data    <= (others => '0');
                timer_2      <= 31;
                t2_expired   <= '0';
                low_speed    <= '0';
                stop_chirp   <= '0';
                latest_chirp_start <= '0';
				usb_busy	 <= '0';
                debug        <= '0';
            end if;
        end if;
    end process;

    gap_length   <= X"10";

end functional;
