library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pll_tb is

end pll_tb;

architecture tb of pll_tb is

    signal clock       : std_logic := '0';
    signal sync_in     : std_logic;
    signal h_sync      : std_logic;
    signal v_sync      : std_logic;

    signal pll_clock   : std_logic := '0';
    signal pll_period  : time := 41 ns;
    signal n           : unsigned(11 downto 0) := to_unsigned(1600-1, 12);
    signal up, down    : std_logic;
    signal analog      : std_logic := 'Z';
    signal pixels_per_line  : integer := 0;
begin
    clock <= not clock after 10 ns;
    pll_clock <= not pll_clock after (pll_period/2);

    p_sync: process
    begin
        for i in 1 to 50 loop
            sync_in <= '0'; wait for 4.7 us;
            sync_in <= '1'; wait for 59.3 us;
        end loop;
        sync_in <= '0'; wait for 4.7 us;
        sync_in <= '1'; wait for 27.3 us;
        for i in 1 to 5 loop
            sync_in <= '0'; wait for 2.35 us;
            sync_in <= '1'; wait for 29.65 us;
        end loop;
        for i in 1 to 5 loop
            sync_in <= '0'; wait for 29.65 us;
            sync_in <= '1'; wait for 2.35 us;
        end loop;
        for i in 1 to 5 loop
            sync_in <= '0'; wait for 2.35 us;
            sync_in <= '1'; wait for 29.65 us;
        end loop;
    end process;


    i_sep: entity work.sync_separator
    port map (
        clock       => clock,
        
        sync_in     => sync_in,
        
        h_sync      => h_sync,
        v_sync      => v_sync );
    
    i_phase: entity work.phase_detector
    port map (
        n           => n,
        pll_clock   => pll_clock,
        h_sync      => h_sync,
    
        up          => up,
        down        => down,
        analog      => analog );

    process(pll_clock, h_sync)
        variable pixel_count    : integer;
    begin
        if rising_edge(pll_clock) then
            pixel_count := pixel_count + 1;
        end if;
        if rising_edge(h_sync) then
            pixels_per_line <= pixel_count;
            pixel_count := 0;
        end if;
    end process;

--    process(analog)
--        variable last       : std_logic := 'U';
--        variable duration   : time;
--        variable last_time  : time;
--    begin
--        if analog'event then
--            duration := now - last_time;
--            case last is
--            when '1' =>
--                report "Up for " & time'image(duration);
--                pll_period <= pll_period - (duration / 5000000);
--            when '0' =>                
--                report "Down for " & time'image(duration);
--                pll_period <= pll_period + (duration / 5000000);
--            when others =>
--                null;
--            end case;
--
--            last := analog;
--            last_time := now;
--        end if;            
--    end process;
    process(clock)
    begin
        if rising_edge(clock) then
            case analog is
            when '1' =>
                pll_period <= pll_period - 10 fs;
            when '0' =>                
                pll_period <= pll_period + 10 fs;
            when others =>
                --pll_period <= pll_period + 1 fs;
                null;
            end case;
        end if;
    end process;                    

end tb;

