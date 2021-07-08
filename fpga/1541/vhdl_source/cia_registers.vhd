library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.cia_pkg.all;
-- synthesis translate_off
use work.tl_string_util_pkg.all;
-- synthesis translate_on

entity cia_registers is
generic (
    g_unit_name     : string := "6526";
    g_report        : boolean := false );
port (
    clock       : in  std_logic;
    falling     : in  std_logic;
    reset       : in  std_logic;
    
    addr        : in  unsigned(3 downto 0);
    wen         : in  std_logic;
    ren         : in  std_logic;
    data_in     : in  std_logic_vector(7 downto 0);
    data_out    : out std_logic_vector(7 downto 0);

    turbo_en    : in  std_logic := '0';
    wait_state  : out std_logic;
    
    -- PLD interface
    pld_inhibit : in  std_logic := '0';
    pld_write   : out std_logic;
    pld_wdata   : out std_logic_vector(7 downto 0);
    pld_waddr   : out std_logic;
    pld_state   : out std_logic_vector(15 downto 0);
    
    -- pio --
    port_a_o    : out std_logic_vector(7 downto 0);
    port_a_t    : out std_logic_vector(7 downto 0);
    port_a_i    : in  std_logic_vector(7 downto 0);
    
    port_b_o    : out std_logic_vector(7 downto 0);
    port_b_t    : out std_logic_vector(7 downto 0);
    port_b_i    : in  std_logic_vector(7 downto 0);

    -- serial pin
    sp_o        : out std_logic;
    sp_i        : in  std_logic;
    sp_t        : out std_logic;
    
    cnt_i       : in  std_logic;
    cnt_o       : out std_logic;
    cnt_t       : out std_logic;
    
    tod_pin     : in  std_logic;
    pc_o        : out std_logic;
    flag_i      : in  std_logic;
    
    irq         : out std_logic );
    
    
end cia_registers;

architecture Gideon of cia_registers is

    signal pio_i        : pio_t    := pio_default;
    signal timer_a_i    : timer_t  := timer_default;
    signal timer_b_i    : timer_t  := timer_default;
    signal tod_i        : tod_t;
    signal tod_copy     : time_t;
    signal tod_latch    : std_logic;
    signal tod_stop     : std_logic;
    signal tod_tick     : std_logic;
    
    signal irq_mask     : std_logic_vector(4 downto 0);
    signal irq_mask_r   : std_logic_vector(4 downto 0);
    signal irq_flags_r  : std_logic_vector(4 downto 0);
    signal irq_flags    : std_logic_vector(4 downto 0);
    signal irq_events   : std_logic_vector(4 downto 0);
    signal irq_out      : std_logic;
    signal irq_bit      : std_logic;
    signal irq_ack      : std_logic;
    signal irq_ack_d    : std_logic := '0';
    
    signal timer_a_reg  : std_logic_vector(15 downto 0);
    signal timer_a_in   : std_logic_vector(15 downto 0);
    signal timer_b_reg  : std_logic_vector(15 downto 0);
    signal timer_b_in   : std_logic_vector(15 downto 0);
    
    alias  timer_a_evnt : std_logic is irq_events(0);
    alias  timer_b_evnt : std_logic is irq_events(1);
    alias  alarm_event  : std_logic is irq_events(2);
    alias  serial_event : std_logic is irq_events(3);
    alias  flag_event   : std_logic is irq_events(4);
    
    signal timer_a_count : std_logic_vector(15 downto 0);
    signal timer_b_count : std_logic_vector(15 downto 0);
    signal timer_a_out   : std_logic;
    signal timer_b_out   : std_logic;
    signal timer_a_stop  : std_logic;
    signal timer_b_stop  : std_logic;
    signal timer_a_set_t : std_logic;
    signal timer_b_set_t : std_logic;

    signal cnt_out      : std_logic;
    signal sp_out       : std_logic;

    signal sr_buffer    : std_logic_vector(7 downto 0);
    signal spmode       : std_logic;

    signal flag_c       : std_logic;
    signal flag_d1      : std_logic;
    signal flag_d2      : std_logic;
    signal flag_d3      : std_logic;

    signal ICR          : std_logic_vector(7 downto 0);
    signal cycle        : natural := 0;
    signal icr_modify   : std_logic := '0';

    signal pld_write_i  : std_logic;
    signal pld_wdata_i  : std_logic_vector(7 downto 0);
    signal pld_waddr_i  : std_logic;

begin
    irq <= irq_out;
    
    process(clock)
        variable v_data_out : std_logic_vector(7 downto 0);
        variable flag_event_pre : std_logic := '0';
    begin
        if rising_edge(clock) then
            if tod_latch='1' then
                tod_copy <= tod_i.tod;
            end if;
            
            -- synthesis translate_off
            if falling = '1' then
                cycle <= cycle + 1;
            end if;
            -- synthesis translate_on
            
            -- Interrupt logic
            if falling = '1' then
                -- by default ICR is not being modified
                icr_modify <= '0';
                
                -- keeping the mask register
                irq_mask_r <= irq_mask;

                -- keeping the flags in a register
                irq_flags_r <= irq_flags;
                if irq_ack = '1' then
                    irq_flags_r <= (others => '0');
                end if;
                if irq_ack_d = '1' then
                    irq_flags_r(1) <= '0'; -- Timer B fix
                end if;
                
                -- the actual IRQ output
                if irq_ack = '1' then
                    irq_out <= '0';
                elsif (irq_flags and irq_mask) /= "00000" then
                    irq_out <= '1';
                end if;
                
                irq_ack_d <= irq_ack;
                
                -- the IRQ bit (MSB of ICR)
                if (irq_flags and irq_mask) /= "00000" then
                    irq_bit <= '1';
                elsif irq_ack = '1' or irq_ack_d = '1' then
                    irq_bit <= '0';
                end if;
            end if;
            
            -- Time Of Day
            if tod_tick='1' and falling='1' then
                do_tod_tick(tod_i.tod);
            end if;
            
            -- PC output --
            if falling='1' then
                pc_o <= '1';
                timer_a_i.load <= '0';
                timer_b_i.load <= '0';
                
                timer_a_reg  <= timer_a_in;
                timer_b_reg  <= timer_b_in;
            end if; 

            if timer_a_stop = '1' then timer_a_i.run <= '0'; end if;
            if timer_b_stop = '1' then timer_b_i.run <= '0'; end if;

            -- Writes --
            if falling = '1' then
                wait_state  <= '0';
            end if;
            
            if wen='1' and falling = '1' then
                -- synthesis translate_off
                assert not g_report
                    report "Writing |" & g_unit_name & "| Reg |" & hstr(addr) & "| in cycle |" & integer'image(cycle) & "| to |" & hstr(data_in)
                    severity note;
                -- synthesis translate_on
                case addr is
                when X"0" => -- PRA
                    pio_i.pra <= data_in;
                                    
                when X"1" => -- PRB
                    pio_i.prb <= data_in;
                    pc_o <= '0';

                when X"2" => -- DDRA
                    pio_i.ddra <= data_in;

                when X"3" => -- DDRB
                    pio_i.ddrb <= data_in;
                    
                when X"4" => -- TA LO
                    
                when X"5" => -- TA HI
                    if timer_a_i.run = '0' then
                        timer_a_i.load <= '1';
                    end if;
                    
                when X"6" => -- TB LO
                    
                when X"7" => -- TB HI
                    if timer_b_i.run = '0' then
                        timer_b_i.load <= '1';
                    end if;
                    
                when X"8" => -- TOD 10ths
                    if tod_i.alarm_set='1' then
                        tod_i.alarm.tenths <= unsigned(data_in(3 downto 0));
                    else
                        tod_i.tod.tenths   <= unsigned(data_in(3 downto 0));
                        tod_stop <= '0';
                    end if;
                
                when X"9" => -- TOD SEC
                    if tod_i.alarm_set='1' then
                        tod_i.alarm.sech <= unsigned(data_in(6 downto 4));
                        tod_i.alarm.secl <= unsigned(data_in(3 downto 0));
                    else
                        tod_i.tod.sech   <= unsigned(data_in(6 downto 4));
                        tod_i.tod.secl   <= unsigned(data_in(3 downto 0));
                    end if;

                when X"A" => -- TOD MIN
                    if tod_i.alarm_set='1' then
                        tod_i.alarm.minh <= unsigned(data_in(6 downto 4));
                        tod_i.alarm.minl <= unsigned(data_in(3 downto 0));
                    else
                        tod_i.tod.minh   <= unsigned(data_in(6 downto 4));
                        tod_i.tod.minl   <= unsigned(data_in(3 downto 0));
                    end if;

                when X"B" => -- TOD HR
                    if tod_i.alarm_set='1' then
                        tod_i.alarm.pm   <= data_in(7);
                        tod_i.alarm.hr   <= unsigned(data_in(4 downto 0));
                    else
                        tod_stop <= '1';
                        -- What an idiocracy! 
                        if data_in(4 downto 0) = "10010" then
                            tod_i.tod.pm <= not data_in(7);
                        else
                            tod_i.tod.pm <= data_in(7);
                        end if;
                        tod_i.tod.hr     <= unsigned(data_in(4 downto 0));
                    end if;

                when X"C" => -- SDR

                when X"D" => -- ICR
                    if data_in(7)='0' then -- clear immediately
                        irq_mask_r <= irq_mask and not data_in(4 downto 0);
                    elsif data_in(7)='1' then -- set
                        irq_mask_r <= irq_mask or data_in(4 downto 0);
                    end if;
                    icr_modify <= '1';
                    
                when X"E" => -- CRA
                    timer_a_i.run     <= data_in(0); -- '1' = run, one-shot underrun clears this bit
                    timer_a_i.pbon    <= data_in(1); -- '1' forces ouput to PB6
                    timer_a_i.outmode <= data_in(2); -- '1' = toggle, '0' = pulse
                    timer_a_i.runmode <= data_in(3); -- '1' = one shot, '0' = cont
                    timer_a_i.load    <= data_in(4); -- pulse
                    timer_a_i.inmode  <= '0' & data_in(5); -- '1' = CNT(r), '0' = PHI2
                    spmode            <= data_in(6); -- '1' = output
                    tod_i.freq_sel    <= data_in(7); -- '1' = 50 Hz
                    wait_state <= turbo_en;

                when X"F" => -- CRB
                    timer_b_i.run     <= data_in(0); -- '1' = run, one-shot underrun clears this bit
                    timer_b_i.pbon    <= data_in(1); -- '1' forces ouput to PB6
                    timer_b_i.outmode <= data_in(2); -- '1' = toggle, '0' = pulse
                    timer_b_i.runmode <= data_in(3); -- '1' = one shot, '0' = cont
                    timer_b_i.load    <= data_in(4); -- pulse
                    timer_b_i.inmode  <= data_in(6 downto 5); -- '01' = CNT(r), '00' = PHI2
                                                                -- '10' = timer_a underflow, '11' = underflow with CNT=high
                    tod_i.alarm_set   <= data_in(7); -- '1' = alarm set, '0' = time set
                    wait_state <= turbo_en;

                when others =>
                    null;
                end case;
            end if;
            
            -- Reads --
            v_data_out := X"FF";
            case addr is
            when X"0" => -- PRA
                v_data_out := port_a_i;
            
            when X"1" => -- PRB
                v_data_out := port_b_i;
                if ren='1' and falling='1' then
                    pc_o <= '0';
                end if;
                
            when X"2" => -- DDRA
                v_data_out := pio_i.ddra;
            
            when X"3" => -- DDRB
                v_data_out := pio_i.ddrb;
                
            when X"4" => -- TA LO
                v_data_out := timer_a_count(7 downto 0);
                
            when X"5" => -- TA HI
                v_data_out := timer_a_count(15 downto 8);
                
            when X"6" => -- TB LO
                v_data_out := timer_b_count(7 downto 0);
                
            when X"7" => -- TB HI
                v_data_out := timer_b_count(15 downto 8);
                
            when X"8" => -- TOD 10ths
                v_data_out := X"0" & std_logic_vector(tod_copy.tenths);
                if ren='1' and falling='1' then
                    tod_latch <= '1';
                end if;
            
            when X"9" => -- TOD SEC
                v_data_out := '0' & std_logic_vector(tod_copy.sech & tod_copy.secl);

            when X"A" => -- TOD MIN
                v_data_out := '0' & std_logic_vector(tod_copy.minh & tod_copy.minl);

            when X"B" => -- TOD HR
                v_data_out := tod_copy.pm & "00" & std_logic_vector(tod_copy.hr);
                if ren='1' and falling = '1' then
                    tod_latch <= '0';
                end if;
                
            when X"C" => -- SDR
                v_data_out := sr_buffer;
                
            when X"D" => -- ICR
                v_data_out := irq_bit & "00" & (irq_flags or irq_events);
                
            when X"E" => -- CRA
                v_data_out(0) := timer_a_i.run; -- '1' = run, one-shot underrun clears this bit
                v_data_out(1) := timer_a_i.pbon   ; -- '1' forces ouput to PB6
                v_data_out(2) := timer_a_i.outmode; -- '1' = toggle, '0' = pulse
                v_data_out(3) := timer_a_i.runmode; -- '1' = one shot, '0' = cont
                v_data_out(4) := '0';
                v_data_out(5) := timer_a_i.inmode(0); -- '1' = CNT(r), '0' = PHI2
                v_data_out(6) := spmode;            -- '1' = output
                v_data_out(7) := tod_i.freq_sel   ; -- '1' = 50 Hz

            when X"F" => -- CRB
                v_data_out(0) := timer_b_i.run; -- '1' = run, one-shot underrun clears this bit
                v_data_out(1) := timer_b_i.pbon   ; -- '1' forces ouput to PB7
                v_data_out(2) := timer_b_i.outmode; -- '1' = toggle, '0' = pulse
                v_data_out(3) := timer_b_i.runmode; -- '1' = one shot, '0' = cont
                v_data_out(4) := '0';
                v_data_out(6 downto 5) := timer_b_i.inmode ; -- '01' = CNT(r), '00' = PHI2
                                                           -- '10' = timer_a underflow, '11' = underflow with CNT=high
                v_data_out(7) := tod_i.alarm_set    ; -- '1' = alarm set, '0' = time set

            when others =>
                null;
            end case;
            data_out <= v_data_out;
            
            -- synthesis translate_off
            assert not (g_report and falling = '1' and ren = '1') 
                report "Reading |" & g_unit_name & "| Reg |" & hstr(addr) & "| in cycle |" & integer'image(cycle) & "| val|" & hstr(v_data_out) 
                severity note;
            -- synthesis translate_on

            flag_c  <= flag_i; -- synchronization flop
            flag_d1 <= flag_c; -- flop 2; output is stable
            flag_d2 <= flag_d1; -- flop 3: for edge detection
            flag_d3 <= flag_d2; -- flop 4: for filtering
             
            if flag_d3 = '1' and flag_d2 = '0' and flag_d1 = '0' then -- falling edge, minimum of two clock cycles low
                flag_event_pre := '1';
            end if;
            if falling = '1' then
                flag_event <= flag_event_pre;
                flag_event_pre := '0';
            end if;
                        
            if reset='1' then
                flag_c    <= '1'; -- resets avoid shift register
                flag_d1   <= '1'; -- resets avoid shift register
                flag_d2   <= '1'; -- resets avoid shift register
                flag_d3   <= '1'; -- resets avoid shift register
                spmode    <= '0';
                pc_o      <= '1';
                tod_latch <= '1';
                tod_stop  <= '1';
                pio_i     <= pio_default;
                tod_i     <= tod_default;
                timer_a_i <= timer_default;
                timer_b_i <= timer_default;
                irq_ack_d   <= '0';
                irq_mask_r  <= (others => '0');
                irq_flags_r <= (others => '0');
                irq_out     <= '0';
                irq_bit     <= '0';
                flag_event  <= '0';
                timer_a_reg <= (others => '1');
                timer_b_reg <= (others => '1');
                wait_state  <= '0';
            end if;
        end if;
    end process;
    
    -- PLD data output
    --process(wen, falling, addr, data_in, pio_i)
    process(clock)
    begin
        if rising_edge(clock) then
            pld_write_i <= '0';
            pld_wdata_i <= (others => '1');
            pld_waddr_i <= '0';
            
            if wen='1' and falling = '1' then
                case addr is
                when X"0" => -- PRA
                    pld_write_i <= '1';
                    pld_wdata_i <= data_in or not pio_i.ddra;
                    pld_waddr_i <= '0'; -- Port A
                                    
                when X"1" => -- PRB
                    pld_write_i <= '1';
                    pld_wdata_i <= data_in or not pio_i.ddrb;
                    pld_waddr_i <= '1'; -- Port B
    
                when X"2" => -- DDRA
                    pld_write_i <= '1';
                    pld_wdata_i <= pio_i.pra or not data_in;
                    pld_waddr_i <= '0'; -- Port A
    
                when X"3" => -- DDRB
                    pld_write_i <= '1';
                    pld_wdata_i <= pio_i.prb or not data_in;
                    pld_waddr_i <= '1'; -- Port B
    
                when others =>
                    null;
                end case;
            end if;
            
            if reset = '1' then
                pld_state <= X"FFFF";
            elsif pld_write_i = '1' then
                if pld_waddr_i = '0' then
                    pld_state(7 downto 0) <= pld_wdata_i;
                else
                    pld_state(15 downto 8) <= pld_wdata_i;
                end if;
            end if;
        end if;
    end process;

    pld_write <= pld_write_i and not pld_inhibit;
    pld_wdata <= pld_wdata_i;
    pld_waddr <= pld_waddr_i;

    -- write through of ta
    process(addr, wen, data_in, timer_a_reg)
    begin
        timer_a_in <= timer_a_reg;
        
        if addr = X"4" and wen = '1' then
            timer_a_in(7 downto 0) <= data_in; 
        end if;
        if addr = X"5" and wen = '1' then
            timer_a_in(15 downto 8) <= data_in;
        end if;
    end process;

    -- write through of tb
    process(addr, wen, data_in, timer_b_reg)
    begin
        timer_b_in <= timer_b_reg;
        
        if addr = X"6" and wen = '1' then
            timer_b_in(7 downto 0) <= data_in; 
        end if;
        if addr = X"7" and wen = '1' then
            timer_b_in(15 downto 8) <= data_in;
        end if;
    end process;

    -- write through of irq_mask, if and only if icr_modify is true
    process(addr, wen, data_in, irq_mask_r, icr_modify)
    begin
        irq_mask <= irq_mask_r;
        
        if addr = X"D" and wen = '1' and icr_modify = '1' then
            if data_in(7)='0' then -- clear immediately
                irq_mask <= irq_mask_r and not data_in(4 downto 0);
            elsif data_in(7)='1' then -- set
                irq_mask <= irq_mask_r or data_in(4 downto 0);
            end if;
        end if;
    end process;

    irq_ack   <= '1' when addr = X"D" and ren = '1' else '0'; 
    irq_flags <= irq_flags_r or irq_events;
    ICR       <= irq_bit & "00" & irq_flags;

    -- combinatoric signal that indicates that the timer is about to start. Needed to set toggle to '1'.
    timer_a_set_t <= '1' when (addr = X"E" and data_in(0) = '1' and wen = '1' and timer_a_i.run = '0') else '0';
    timer_b_set_t <= '1' when (addr = X"F" and data_in(0) = '1' and wen = '1' and timer_b_i.run = '0') else '0';

    -- Implement tod pin synchronization, edge detect and programmable prescaler
    -- In addition, implement alarm compare logic with edge detect for event generation
    
    b_tod: block
        signal tod_c, tod_d : std_logic := '0';
        signal tod_pre      : unsigned(2 downto 0);
        signal alarm_equal  : boolean;
        signal alarm_equal_d: boolean;
    begin
        -- alarm --
        alarm_equal   <= (tod_i.alarm = tod_i.tod);

        --alarm_event   <= '1' when alarm_equal else '0';
         
        p_tod: process(clock)
        begin
            if rising_edge(clock) then
                if falling = '1' then
                    tod_c    <= tod_pin;
                    tod_d    <= tod_c;
                    tod_tick <= '0';
                    if tod_stop = '0' then
                        if tod_c = '1' and tod_d = '0' then
--                            if (tod_pre = "100" and tod_i.freq_sel='1') or
--                               (tod_pre = "101" and tod_i.freq_sel='0') then
--                                tod_pre <= "000";
--                                tod_tick <= '1';
--                            else
--                                tod_pre <= tod_pre + 1;
--                            end if;
                            if tod_pre = "000" then
                                tod_tick <='1';
                                tod_pre <= "10" & not tod_i.freq_sel;
                            else
                                tod_pre <= tod_pre - 1;
                            end if;
                        end if;
                    else
                        tod_pre <= "10" & not tod_i.freq_sel;
--                        tod_pre <= "000";
                    end if;

                    -- alarm --
                    alarm_equal_d <= alarm_equal;
                    alarm_event   <= '0';
                    if alarm_equal and not alarm_equal_d then
                        alarm_event <= '1';
                    end if;
                        
                end if;
                
                if reset='1' then
                    tod_pre <= "000";
                    tod_tick <= '0';
                    alarm_event <= '0';
                end if;
            end if;
        end process;
    end block;

    -- PIO Out select --
    port_a_o             <= pio_i.pra;
    port_b_o(5 downto 0) <= pio_i.prb(5 downto 0);    
    port_b_o(6) <= pio_i.prb(6) when timer_a_i.pbon='0' else timer_a_out;
    port_b_o(7) <= pio_i.prb(7) when timer_b_i.pbon='0' else timer_b_out;
    
    port_a_t             <= pio_i.ddra;
    port_b_t(5 downto 0) <= pio_i.ddrb(5 downto 0);
    port_b_t(6)          <= pio_i.ddrb(6) or timer_a_i.pbon;
    port_b_t(7)          <= pio_i.ddrb(7) or timer_b_i.pbon;
    
    -- Timer A
    tmr_a: entity work.cia_timer
    port map (
        clock       => clock,
        reset       => reset,
        prescale_en => falling,
        
        timer_ctrl  => timer_a_i,
        timer_set_t => timer_a_set_t,
        timer_in    => timer_a_in,
        
        cnt         => cnt_i,
        othr_tmr_ev => '0',
        
        timer_out   => timer_a_out,
        timer_stop  => timer_a_stop,
        timer_event => timer_a_evnt,
        timer_count => timer_a_count );
    
    -- Timer B
    tmr_b: entity work.cia_timer
    port map (
        clock       => clock,
        reset       => reset,
        prescale_en => falling,
        
        timer_ctrl  => timer_b_i,
        timer_set_t => timer_b_set_t,
        timer_in    => timer_b_in,
        
        cnt         => cnt_i,
        othr_tmr_ev => timer_a_evnt,
        
        timer_out   => timer_b_out,
        timer_stop  => timer_b_stop,
        timer_event => timer_b_evnt,
        timer_count => timer_b_count );

    
    ser: block
        signal bit_cnt      : integer range 0 to 7;
        signal sr_shift     : std_logic_vector(7 downto 0);
        signal transmitting : std_logic := '0';
        signal cnt_d1, cnt_d2, cnt_c : std_logic;
        signal spmode_d     : std_logic;
        signal sr_dav       : std_logic;
        signal timer_a_evnt_d1  : std_logic := '0';
        signal timer_a_evnt_d2  : std_logic := '0';
        signal transmit_event   : std_logic := '0';
        signal transmit_event_d1: std_logic := '0';
        signal receive_event    : std_logic := '0';
        signal receive_event_d1 : std_logic := '0';
    begin
        process(clock)
        begin
            if rising_edge(clock) then
                if falling = '1' then
                    cnt_c        <= cnt_i;
                    cnt_d1       <= cnt_c;
                    cnt_d2       <= cnt_d1;
                    spmode_d     <= spmode;
                    receive_event <= '0';
                    transmit_event <= '0';
                    
                    timer_a_evnt_d1 <= timer_a_evnt and transmitting;
                    timer_a_evnt_d2 <= timer_a_evnt_d1;

                    if spmode = '1' then -- output
                        if timer_a_evnt_d2='1' then
                            cnt_out <= not cnt_out;
                            if cnt_out='1' then -- was '1' -> falling edge
                                sp_out <= sr_shift(7);
                                sr_shift <= sr_shift(6 downto 0) & '0';
                                if bit_cnt = 0 then
                                    transmit_event <= '1';
                                end if;
                            else
                                if bit_cnt=0 then
                                    if sr_dav='1' then
                                        sr_dav <= '0';
                                        bit_cnt <= 7;
                                        sr_shift <= sr_buffer;
                                        transmitting <= '1';
                                    else
                                        transmitting <= '0';
                                    end if;
                                else
                                    bit_cnt <= bit_cnt - 1;
                                end if;
                            end if;
                        elsif sr_dav = '1' and transmitting = '0' then
                            sr_dav <= '0';
                            bit_cnt <= 7;
                            sr_shift <= sr_buffer;
                            transmitting <= '1';
                        end if;
                    else -- input mode
                        cnt_out <= '1';
                        if cnt_d2='0' and cnt_d1='1' then
                            sr_shift <= sr_shift(6 downto 0) & sp_i;
                            if bit_cnt = 0 then
                                bit_cnt   <= 7;
                                receive_event <= '1';
                            else
                                bit_cnt <= bit_cnt - 1;
                            end if;
                        end if;
                    end if;
    
                    if wen='1' and addr=X"C" then
                        sr_dav <= '1';
                        sr_buffer  <= data_in;
                    elsif receive_event = '1' then
                        sr_buffer  <= sr_shift;
--                    elsif wen='1' and addr=X"E" then
--                        if transmitting='1' and data_in(6)='0' and spmode='1' then -- switching to input while transmitting
--                            sr_buffer <= not sr_shift; -- ?
--                        end if;
                    end if;
    
                    -- when switching to read mode, we assume there are 8 bits coming
                    if spmode='0' and spmode_d='1' then
                        sr_dav <= '0';
                        bit_cnt <= 7;
                        transmitting <= '0';
                    end if;

                    transmit_event_d1 <= transmit_event;
                    --transmit_event_d2 <= transmit_event_d1;
                    receive_event_d1  <= receive_event;
                    --receive_event_d2  <= receive_event_d1;
                end if;
                
                if reset='1' then
                    cnt_out      <= '1';
                    sp_out       <= '0';
                    bit_cnt      <= 7;
                    transmitting <= '0';
                    sr_dav       <= '0';
                    sr_shift     <= (others => '0');
                    sr_buffer    <= (others => '0');
                    transmit_event    <= '0';
                    transmit_event_d1 <= '0';
                    receive_event     <= '0';
                    receive_event_d1  <= '0';
                end if;
            end if;
        end process;
        
        serial_event <= receive_event_d1 or transmit_event_d1 or
                        '0';
                        -- (spmode and not spmode_d and not sr_dav) or -- when switching to output, and there is no data in the buffer
--                        (not spmode and spmode_d and transmitting); -- when switching to input, and we are still in the transmit state 
        
    end block ser;

    -- open drain
    cnt_o <= '0';
    sp_o  <= '0';
    cnt_t <= spmode and not cnt_out;
    sp_t  <= spmode and not sp_out;

end Gideon;
