library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ulpi_bus_ctrl is
port (
    clock       : in    std_logic;
    reset       : in    std_logic;
    
    do_reset    : in    std_logic;

    tx_request  : out   std_logic; -- sets selector
    tx_ack      : in    std_logic; -- we have the ulpi bus
    
    reset_done  : out   std_logic;
    speed       : out   std_logic_vector(1 downto 0);
    
    -- status
    status      : in    std_logic_vector(7 downto 0);

    tx_data     : out   std_logic_vector(7 downto 0);
    tx_last     : out   std_logic;
    tx_start    : out   std_logic;
    tx_valid    : out   std_logic;
    tx_next     : in    std_logic );

end ulpi_bus_ctrl;

architecture functional of ulpi_bus_ctrl is
    type t_state is (idle, set_se0, set_se0_2, listen_chirp,
                     wait_chirp_end, setup_chirp, hub_chirp_k, hub_chirp_j,
                     reset_end, reset_finished );

    signal state        : t_state;                    
    signal tx_req_i     : std_logic;
    signal speed_i      : std_logic_vector(1 downto 0);
    signal low_speed    : std_logic;
    signal t0_expired   : std_logic;
    signal timer_0      : integer range 0 to 2097151; -- ~  35 ms
    signal timer_1      : integer range 0 to 8191;    -- ~ 136 µs
    signal stop_chirp   : std_logic;
    signal latest_chirp_start : std_logic;
begin
    speed      <= speed_i;
    tx_request <= tx_req_i;
    
    p_reset: process(clock)
    begin
        if rising_edge(clock) then
            if timer_0 = 0 then
                t0_expired <= '1';
            else
                timer_0 <= timer_0 - 1;
            end if;
            if timer_0 = 20000 then
                stop_chirp <= '1';
            end if;
            if timer_0 = 80000 then
                latest_chirp_start <= '1';
            end if;
            
            case state is
            when idle =>
                tx_data <= X"00";
                timer_0 <= 2097151;
                t0_expired <= '0';
                stop_chirp <= '0';
                latest_chirp_start <= '0';
        
                if do_reset='1' then
                    reset_done <= '0';
                    low_speed <= '0';
                    if status(5 downto 2) /= "0011" then
                        speed_i <= "11"; -- not powered or rx active
                    else
                        if status(1)='1' then
                            low_speed <= '1';
                            speed_i <= "00"; -- Low speed
                        else
                            speed_i <= "01"; -- assume FS
                        end if;
                        tx_req_i <= '1';
                    end if;
                end if;

                if tx_req_i = '1' and tx_ack = '1' then
                    state <= set_se0;
                    t0_expired <= '0'; -- dummy
                end if;

            when set_se0 =>
                tx_data  <= X"84"; -- write ulpi register address 04
                tx_start <= '1';
                tx_valid <= '1';
                tx_last  <= '0';
                if tx_next = '1' then
                    tx_data <= X"50"; -- set High speed Chirp mode, (SE0)
                    tx_start <= '0';
                    tx_last  <= '1';
                    state <= set_se0_2;
                end if;
            
            when set_se0_2 =>
                timer_1 <= 255; -- reset timer 1 (4.25 µs)
                if tx_next='1' then
                    tx_valid <= '0';
                    tx_last  <= '0';
                    if low_speed='1' then
                        state <= reset_end;
                    else
                        state <= listen_chirp;
                    end if;
                end if;

            when listen_chirp =>
                if t0_expired='1' then
                    state <= reset_end; -- no chirp detected
                elsif status(1)='0' then
                    timer_1 <= 255; -- reset timer
                elsif timer_1 = 0 then -- chirp detected
                    speed_i <= "10"; -- HS!
                    state <= wait_chirp_end;
                    timer_1 <= 6000;
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
                    timer_1 <= 6000; -- reset timer
                end if;

            when setup_chirp =>
                timer_1 <= 3000;
                tx_data <= X"40"; -- send PIDless packet
                tx_valid <= '1';
                tx_start <= '1';
                tx_last  <= '0';
                if tx_next='1' then
                    tx_data <= X"00";
                    tx_start <= '0';
                    state <= hub_chirp_k;
                end if;

            when hub_chirp_k =>
                if timer_1 = 0 then
                    if stop_chirp = '1' then
                        state <= reset_end;
                        tx_last <= '1'; -- data is still 0
                    else
                        tx_data <= X"FF";
                        state <= hub_chirp_j;
                        timer_1 <= 3000;
                    end if;
                else
                    timer_1 <= timer_1 - 1;
                end if;

            when hub_chirp_j =>
                if timer_1 = 0 then
                    timer_1 <= 3000;
                    tx_data <= X"00";
                    state <= hub_chirp_k;
                else
                    timer_1 <= timer_1 - 1;
                end if;

            when reset_end =>
                if t0_expired = '1' then
                    tx_last  <= '0';
                    tx_start <= '1';
                    tx_valid <= '1';
                    tx_data  <= X"84"; -- write ulpi register address 04
                    if tx_next='1' then
                        tx_last <= '1';
                        tx_start <= '0';
                        case speed_i is
                        when "00" =>
                            tx_data <= X"66"; -- reset in LS mode
                        when "01" =>
                            tx_data <= X"65"; -- reset in FS mode
                        when "10" =>
                            tx_data <= X"60"; -- reset in HS mode
                        when others =>
                            tx_data <= X"50"; -- stay in chirp mode
                        end case;
                        state <= reset_finished;
                    end if;
                end if;
            
            when reset_finished =>
                if tx_next='1' then
                    tx_valid <= '0';
                    reset_done <= '1';
                    tx_req_i <= '0';
                    state <= idle;
                end if;
            
            when others =>
                null;
            end case;

            if reset = '1' then
                speed_i    <= "11"; -- error or uninitialized
                state      <= idle;
                tx_req_i   <= '0';
                reset_done <= '0';
                tx_data    <= X"00";
                tx_start   <= '0';
                tx_last    <= '0';
                tx_valid   <= '0';
            end if;
        end if;
    end process;
                 

end functional;
