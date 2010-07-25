library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity tb_via6522 is
end tb_via6522;

architecture tb of tb_via6522 is
    signal clock       : std_logic := '0';
    signal clock_en    : std_logic := '0'; -- for counters and stuff
    signal reset       : std_logic;
    signal addr        : std_logic_vector(3 downto 0) := X"0";
    signal wen         : std_logic := '0';
    signal ren         : std_logic := '0';
    signal data_in     : std_logic_vector(7 downto 0) := X"00";
    signal data_out    : std_logic_vector(7 downto 0) := X"00";
    signal irq         : std_logic;

    -- pio --
    signal port_a_o    : std_logic_vector(7 downto 0);
    signal port_a_t    : std_logic_vector(7 downto 0);
--    signal port_a_i    : std_logic_vector(7 downto 0);
    signal port_b_o    : std_logic_vector(7 downto 0);
    signal port_b_t    : std_logic_vector(7 downto 0);
--    signal port_b_i    : std_logic_vector(7 downto 0);

    -- handshake pins
--    signal ca1_i       : std_logic;
    signal ca2_o       : std_logic;
--    signal ca2_i       : std_logic;
    signal ca2_t       : std_logic;
    signal cb1_o       : std_logic;
--    signal cb1_i       : std_logic;
    signal cb1_t       : std_logic;
    signal cb2_o       : std_logic;
--    signal cb2_i       : std_logic;
    signal cb2_t       : std_logic;

    signal ca1, ca2    : std_logic;
    signal cb1, cb2    : std_logic;
    signal port_a      : std_logic_vector(7 downto 0);
    signal port_b      : std_logic_vector(7 downto 0);

begin
    port_a <= (others => 'H');
    port_b <= (others => 'H');
    ca1    <= 'H';
    ca2    <= ca2_o when ca2_t='1' else 'H';
    cb1    <= cb1_o when cb1_t='1' else 'H';
    cb2    <= cb2_o when cb2_t='1' else 'H';

    process(port_a_o, port_a_t, port_b_o, port_b_t)
    begin
        for i in 0 to 7 loop
            if port_a_t(i)='1' then
                port_a(i) <= port_a_o(i);
            else
                port_a(i) <= 'H';
            end if;
            if port_b_t(i)='1' then
                port_b(i) <= port_b_o(i);
            else
                port_b(i) <= 'H';
            end if;
        end loop;
    end process;

    via: entity work.via6522
    port map (
        clock       => clock,
        clock_en    => clock_en, -- for counters and stuff
        reset       => reset,
                                
        addr        => addr,
        wen         => wen,
        ren         => ren,
        data_in     => data_in,
        data_out    => data_out,
                                
        -- pio --   
        port_a_o    => port_a_o,
        port_a_t    => port_a_t,
        port_a_i    => port_a,
                                
        port_b_o    => port_b_o,
        port_b_t    => port_b_t,
        port_b_i    => port_b,
    
        -- handshake pins
        ca1_i       => ca1,
                            
        ca2_o       => ca2_o,
        ca2_i       => ca2,
        ca2_t       => ca2_t,
                            
        cb1_o       => cb1_o,
        cb1_i       => cb1,
        cb1_t       => cb1_t,
                            
        cb2_o       => cb2_o,
        cb2_i       => cb2,
        cb2_t       => cb2_t,
                            
        irq         => irq  );
    
    clock <= not clock after 125 ns;
    reset <= '1', '0' after 2 us;

    ce: process
    begin
        clock_en <= '0';
        wait until clock='1';
        wait until clock='1';
        wait until clock='1';
        clock_en <= '1';
        wait until clock='1';
    end process;
    
    test: process
        procedure do_write(a: std_logic_vector(3 downto 0); d: std_logic_vector(7 downto 0)) is
        begin
            wait until clock='1';
            addr    <= a;
            data_in <= d;
            wen     <= '1';
            wait until clock='1';
            wen     <= '0';
        end do_write;

        procedure do_read(a: std_logic_vector(3 downto 0); d: out std_logic_vector(7 downto 0)) is
        begin
            wait until clock='1';
            addr    <= a;
            ren     <= '1';
            wait until clock='1';
            wait for 1 ns;
            ren     <= '0';
            d       := data_out;
        end do_read;

        variable start      : time;
        variable read_data  : std_logic_vector(7 downto 0);
        constant test_byte  : std_logic_vector(7 downto 0) := X"47";
        constant test_byte2 : std_logic_vector(7 downto 0) := X"E2";
    begin
        ca1 <= 'Z';
        ca2 <= 'Z';
        cb1 <= 'Z';
        cb2 <= 'Z';
        port_b <= (others => 'Z');
        
        wait until reset='0';
        for i in 0 to 15 loop
            do_read(conv_std_logic_vector(i, 4), read_data);
        end loop;
        do_write(X"0", X"55"); -- set data = 55
        do_write(X"2", X"33"); -- set direction = 33
        do_read (X"0", read_data);
        assert read_data = "HH01HH01" report "Data port B seems wrong" severity error;

        do_write(X"1", X"99"); -- set data = 99
        do_write(X"3", X"AA"); -- set direction = AA
        do_read (X"0", read_data);
        assert read_data = "1H0H1H0H" report "Data port A seems wrong" severity error;

       -- TEST SHIFT REGISTER --

        do_write(X"8", X"05"); -- timer 2 latch = 5
        do_write(X"E", X"84"); -- enable IRQ on shift register
        do_write(X"B", X"04"); -- Shift Control = 1 (shift in on timer 2)
        do_write(X"A", X"00"); -- dummy write to SR, to start transfer

        for i in 7 downto 0 loop
            wait until cb1='0';
            cb2 <= test_byte(i);
            if i = 7 then
                start := now;
            end if;
        end loop;
        wait until cb1='1';
        cb2 <= 'Z';
        wait until irq='1';

        assert integer((now - start)/7 us) = 12 report "Timing error serial mode 1." severity error;
--        report "Receiving byte. Bit time: " & integer'image(integer((now - start)/7 us)) severity note;

        do_write(X"B", X"08"); -- Shift Control = 2 (shift in on system clock)
        do_read (X"A", read_data); -- check byte from previous transmit

        assert read_data = test_byte report "Data byte came in was not correct (mode 1)." severity error;
        
        for i in 7 downto 0 loop
            wait until cb1='0';
            cb2 <= not test_byte(i);
            if i = 7 then
                start := now;
            end if;
        end loop;
        wait until cb1='1';
        cb2 <= 'Z';
        wait until irq='1';
        assert integer((now - start)/7 us) = 2 report "Timing error serial mode 2." severity error;

        do_write(X"B", X"0C"); -- Shift Control = 3 (shift in under control of cb1)
        do_read (X"A", read_data); -- check byte from previous transmitm, trigger new

        assert read_data = not test_byte report "Data byte came in was not correct (mode 2)." severity error;
        
        for i in test_byte2'range loop
            cb1 <= '0';
            wait for 2 us;
            cb2 <= test_byte2(i);
            wait for 2 us;
            cb1 <= '1';
            wait for 2 us;
        end loop;
        cb2 <= 'Z';
        cb1 <= 'Z';
                 
        do_write(X"B", X"10"); -- Shift Control = 4 (shift out continuously)
        do_read (X"A", read_data); -- check byte from previous transmitm, trigger new

        assert read_data = test_byte2 report "Data byte came in was not correct (mode 3)." severity error;

        wait for 150 us;
        assert irq = '0' report "An IRQ was generated, but not expected.";
        
        do_write(X"B", X"00"); -- stop endless loop
        
        do_write(X"8", X"03"); -- timer 2 latch = 3 (8 us per bit)
        do_write(X"B", X"14"); -- Shift Control = 5 (shift out on Timer 2)

        do_write(X"A", X"55");
        
        for i in 7 downto 0 loop
            wait until cb1='1';
            read_data(i) := cb2;
        end loop;
        wait until irq='1';

        assert read_data = X"55" report "Data byte sent out was not correct (mode 5)." severity error;
                
        do_write(X"B", X"18"); -- Shift Control = 6 (shift out on system clock)
        do_write(X"A", X"81");
        
        for i in 7 downto 0 loop
            wait until cb1='1';
            read_data(i) := cb2;
        end loop;
        wait until irq='1';
        
        assert read_data = X"81" report "Data byte sent out was not correct (mode 6)." severity error;

        do_write(X"B", X"1c"); -- Shift Control = 7 (shift out on own clock)
        do_write(X"A", X"B3"); 

        for i in 7 downto 0 loop
            cb1 <= '0';
            wait for 2 us;
            read_data(i) := cb2;
            cb1 <= '1';
            wait for 2 us;
        end loop;
        cb1 <= 'Z';

        assert read_data = X"B3" report "Data byte sent out was not correct (mode 7)." severity error;        

        do_write(X"B", X"00"); -- disable shift register
        
        do_write(X"E", X"7F"); -- clear all interupt enable flags

        -- TEST TIMER 1 --

        do_write(X"E", X"C0"); -- enable interrupt on Timer 1
        
        -- timer 1 is now in one shot mode, output disabled
        do_write(X"4", X"30"); -- Set timer to 0x230
        do_write(X"5", X"02"); -- ... and start one shot
        start := now;
                
        wait until irq='1';
        assert integer((now - start)/ 1 us) = 561 report "Interrupt of timer 1 received. Duration Error."  severity error;

        do_read (X"4", read_data);
        wait until clock='1';
        assert irq = '0' report "Expected interrupt to be cleared by reading address 4." severity error;
        
        do_write(X"B", X"40"); -- timer in cont. mode
        do_write(X"4", X"20"); -- timer = 0x120
        do_write(X"5", X"01"); -- trigger, and go
        
        wait until irq='1';
        start := now;
        do_read(X"4", read_data);
        wait until irq='1';
--        report integer'image(integer((now - start) / 1 us)) severity note;
        assert integer((now - start) / 1 us) = 290 report "Timer 1 continuous mode, interrupt distance wrong." severity error;
        
        do_write(X"B", X"80"); -- timer 1 one shot, PB7 enabled
        do_write(X"4", X"44"); -- set timer to 0x0044
        assert irq = '1' report "Expected IRQ still to be set" severity error;
        do_write(X"5", X"00"); -- set timer, clear flag, go!
        start := now;
        wait until clock='1';
        assert irq = '0' report "Expected IRQ to be cleared" severity error;
        wait until irq='1';
                                
--        report integer'image(integer((now - start) / 1 us)) severity note;
        assert integer((now - start) / 1 us) = 68 report "Timer 1 one shot output mode, interrupt distance wrong." severity error;

        do_write(X"B", X"C0"); -- timer 1 continuous, PB7 enabled
        do_write(X"4", X"24"); -- set timer to 0x0024
        do_write(X"5", X"00"); -- set timer, clear flag, go!
        start := now;
        wait until irq='1';
        assert port_b(7)='1' report "Expected bit 7 of PB to be '1'" severity error;
        do_write(X"7", X"00"); -- re-write latch value, reset flag
        wait until irq='1';
        assert port_b(7)='0' report "Expected bit 7 of PB to be '0'" severity error;
        do_read(X"4", read_data); --reset flag
        wait until irq='1';
        assert port_b(7)='1' report "Expected bit 7 of PB to be '1'" severity error;

        do_write(X"B", X"00"); -- timer 1 one shot, output disabled
        do_write(X"E", X"7E"); -- clear interrupt enable flags
                
        -- TEST TIMER 2 --
        do_write(X"E", X"A0"); -- Set interrupt on timer 2
        do_write(X"8", X"33"); -- Set lower latch to 33.
        wait for 10 us;        -- observe timer to count
        wait until clock_en='1';
        do_write(X"9", X"02"); -- Set timer to 0x233 and wait for IRQ
        start := now;
        wait until irq='1';

--        report integer'image(integer((now - start) / 1 us)) severity note;
        assert integer((now - start) / 1 us) = 16#233# report "Timer 2 one shot mode, interrupt time wrong." severity error;

        do_read(X"8", read_data);
        
        do_write(X"B", X"20"); -- set to pulse count mode
        do_write(X"2", X"00"); -- set port B to input
        do_write(X"8", X"0A"); -- set to 10 pulses
        do_write(X"9", X"00"); -- high byte and trigger
        
        for i in 0 to 10 loop
            port_b(6) <= '0';
            wait for 5 us;
            port_b(6) <= '1';
            wait for 1 us;
            assert not((i > 9) and (irq = '0')) report "Expected IRQ to be 1 after 10th pulse" severity error; 
            assert not((i < 10) and (irq = '1')) report "Expected IRQ to be 0 before 10th pulse" severity error; 
            wait for 15 us;
        end loop;            

        -- TEST CA1 --
        do_write(X"C", X"00");
        do_write(X"E", X"7F");
        do_write(X"E", X"82"); -- interrupt on CA1
        wait until clock='1';
        -- no transitions have taken place yet on CA1, hence IRQ should be low
        assert irq='0' report "Expected CA1 interrupt to be low before any transition." severity error;
        
        ca1 <= '0';
        wait for 2 us;
        assert irq='1' report "Expected CA1 IRQ to be set after negative transition." severity error;
        
        do_read(X"1", read_data);
        wait for 2 us;
        assert irq='0' report "Expected CA1 IRQ to be cleared by reading port a." severity error;
        
        do_write(X"C", X"01"); -- CA1 control = '1', expecting rising edge
        wait for 2 us;
        ca1 <= '1';
        wait for 2 us;
        assert irq='1' report "Expected CA1 IRQ to be set after positive transition." severity error;
        
        do_write(X"1", X"47");
        wait for 2 us;
        assert irq='0' report "Expected CA1 IRQ to be cleared by writing port A." severity error;
                
        -- TEST CB1 --
        cb1 <= '1';
        do_write(X"0", X"11"); -- clear flag
        do_write(X"C", X"00");
        do_write(X"E", X"7F");
        do_write(X"E", X"90"); -- interrupt on CB1
        wait until clock='1';
        -- no transitions have taken place yet on CB1, hence IRQ should be low
        assert irq='0' report "Expected CB1 interrupt to be low before any transition." severity error;
        
        cb1 <= '0';
        wait for 2 us;
        assert irq='1' report "Expected CB1 IRQ to be set after negative transition." severity error;
        
        do_read(X"0", read_data);
        wait for 2 us;
        assert irq='0' report "Expected CB1 IRQ to be cleared by reading port B." severity error;
        
        do_write(X"C", X"10"); -- CB1 control = '1', expecting rising edge
        wait for 2 us;
        cb1 <= '1';
        wait for 2 us;
        assert irq='1' report "Expected CB1 IRQ to be set after positive transition." severity error;
        
        do_write(X"0", X"47");
        wait for 2 us;
        assert irq='0' report "Expected CB1 IRQ to be cleared by writing port B." severity error;

        -- TEST CA2 --
        -- mode 0: input, negative transition, Port A out clears flag
        ca2 <= '1';
        do_write(X"C", X"00"); -- mode 0
        do_write(X"D", X"01"); -- clear flag
        do_write(X"E", X"7F"); -- reset all interrupt enables
        do_write(X"E", X"81"); -- enable CA2 interrupt
        wait for 2 us;
        assert irq='0' report "Expected CA2 interrupt to be low before any transition." severity error;
        ca2 <= '0';
        wait for 2 us;
        assert irq='1' report "Expected CA2 IRQ to be set after negative transition." severity error;

        do_write(X"1", X"44"); -- write to Port a
        wait for 2 us;
        assert irq='0' report "Expected CA2 IRQ to be cleared by writing to port A." severity error;
        
        -- mode 2: input, positive transition, Port A in/out clears flag
        do_write(X"C", X"04"); -- mode 2
        wait for 2 us;
        ca2 <= '1';
        wait for 2 us;
        assert irq='1' report "Expected CA2 IRQ to be set after positive transition." severity error;
        
        do_read(X"1", read_data);
        wait for 2 us;
        assert irq='0' report "Expected CA2 IRQ to be cleared by reading port A." severity error;
        
        -- mode 1 / 3, read/write to port A does NOT clear the interrupt flag
        do_write(X"C", X"02"); -- mode 1
        wait for 2 us;
        ca2 <= '0';
        wait for 2 us;
        assert irq='1' report "Expected CA2 IRQ to be set after negative transition (mode 1)." severity error;
        do_read(X"1", read_data);
        wait for 2 us;
        assert irq='1' report "Expected CA2 IRQ to be STILL set after negative transition (mode 1)." severity error;
        do_write(X"D", X"01"); -- clear flag manually

        do_write(X"C", X"06"); -- mode 3
        wait for 2 us;
        ca2 <= '1';
        wait for 2 us;
        assert irq='1' report "Expected CA2 IRQ to be set after positive transition (mode 3)." severity error;
        do_read(X"1", read_data);
        wait for 2 us;
        assert irq='1' report "Expected CA2 IRQ to be STILL set after positive transition (mode 3)." severity error;
        do_write(X"D", X"01"); -- clear flag manually
                    
        -- mode 4
        ca2 <= 'Z';
        do_write(X"C", X"08"); -- mode 4
        
        do_write(X"1", X"31"); -- write to Port A
        wait for 2 us;
        assert ca2 = '0' report "Expected CA2 to have gone low upon writing to Port A (mode 4)." severity error;
        
        ca1 <= '0';
        wait for 2 us;
        assert ca2 = '1' report "Expected CA2 to have gone high upon active transition on CA1 (mode 4)." severity error;

        ca1 <= '1';
        wait for 2 us;

        -- mode 5
        do_write(X"C", X"0A"); -- mode 5
        wait until clock_en='1';
        do_write(X"1", X"32"); -- write to port A
        wait until clock_en='1' and clock='1';
        wait for 1 ns;
        assert ca2 = '0' report "Expected CA2 to have gone low upon writing to Port A (mode 5)." severity error;
        wait until clock_en='1' and clock='1';
        wait for 1 ns;
        assert ca2 = '1' report "Expected CA2 to have gone high after one cycle (mode 5)." severity error;
        
        -- mode 6
        do_write(X"C", X"0C"); -- mode 6
        wait for 2 us;
        assert ca2 = '0' report "Expected CA2 to be low in mode 6" severity error;

        -- mode 7
        do_write(X"C", X"0E"); -- mode 7
        wait for 2 us;
        assert ca2 = '1' report "Expected CA2 to be high in mode 7." severity error;


        -- TEST CB2 --
        -- mode 0: input, negative transition, Port B out clears flag
        cb2 <= '1';
        do_write(X"C", X"00"); -- mode 0
        do_write(X"D", X"08"); -- clear flag
        do_write(X"E", X"7F"); -- reset all interrupt enables
        do_write(X"E", X"88"); -- enable CB2 interrupt
        wait for 2 us;
        assert irq='0' report "Expected CB2 interrupt to be low before any transition." severity error;
        cb2 <= '0';
        wait for 2 us;
        assert irq='1' report "Expected CB2 IRQ to be set after negative transition." severity error;

        do_write(X"0", X"44"); -- write to Port B
        wait for 2 us;
        assert irq='0' report "Expected CB2 IRQ to be cleared by writing to port B." severity error;
        
        -- mode 2: input, positive transition, Port B in/out clears flag
        do_write(X"C", X"40"); -- mode 2
        wait for 2 us;
        cb2 <= '1';
        wait for 2 us;
        assert irq='1' report "Expected CB2 IRQ to be set after positive transition." severity error;
        
        do_read(X"0", read_data);
        wait for 2 us;
        assert irq='0' report "Expected CB2 IRQ to be cleared by reading port B." severity error;
        
        -- mode 1 / 3, read/write to port B does NOT clear the interrupt flag
        do_write(X"C", X"20"); -- mode 1
        wait for 2 us;
        cb2 <= '0';
        wait for 2 us;
        assert irq='1' report "Expected CB2 IRQ to be set after negative transition (mode 1)." severity error;
        do_read(X"0", read_data);
        wait for 2 us;
        assert irq='1' report "Expected CB2 IRQ to be STILL set after negative transition (mode 1)." severity error;
        do_write(X"D", X"08"); -- clear flag manually

        do_write(X"C", X"60"); -- mode 3
        wait for 2 us;
        cb2 <= '1';
        wait for 2 us;
        assert irq='1' report "Expected CB2 IRQ to be set after positive transition (mode 3)." severity error;
        do_read(X"0", read_data);
        wait for 2 us;
        assert irq='1' report "Expected CB2 IRQ to be STILL set after positive transition (mode 3)." severity error;
        do_write(X"D", X"08"); -- clear flag manually
                    
        -- mode 4
        cb2 <= 'Z';
        do_write(X"C", X"80"); -- mode 4
        
        do_write(X"0", X"31"); -- write to Port B
        wait for 2 us;
        assert cb2 = '0' report "Expected CB2 to have gone low upon writing to Port B (mode 4)." severity error;
        
        cb1 <= '0';
        wait for 2 us;
        assert cb2 = '1' report "Expected CB2 to have gone high upon active transition on CB1 (mode 4)." severity error;

        cb1 <= '1';
        wait for 2 us;

        -- mode 5
        do_write(X"C", X"A0"); -- mode 5
        wait until clock_en='1';
        do_write(X"0", X"32"); -- write to port B
        wait until clock_en='1' and clock='1';
        wait for 1 ns;
        assert cb2 = '0' report "Expected CB2 to have gone low upon writing to Port B (mode 5)." severity error;
        wait until clock_en='1' and clock='1';
        wait for 1 ns;
        assert cb2 = '1' report "Expected CB2 to have gone high after one cycle (mode 5)." severity error;
        
        -- mode 6
        do_write(X"C", X"C0"); -- mode 6
        wait for 2 us;
        assert cb2 = '0' report "Expected CB2 to be low in mode 6" severity error;

        -- mode 7
        do_write(X"C", X"E0"); -- mode 7
        wait for 2 us;
        assert cb2 = '1' report "Expected CB2 to be high in mode 7." severity error;


        wait;
    end process;

end tb;
