-------------------------------------------------------------------------------
-- Date       $Date: 2005/04/12 19:09:27 $
-- Author     $Author: Gideon $
-- Revision   $Revision: 1.1 $
-- Log        $Log: oscillator.vhd,v $
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_sid is

end tb_sid;

architecture tb of tb_sid is

    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal addr        : unsigned(6 downto 0) := (others => '0');
    signal wren        : std_logic := '0';
    signal wdata       : std_logic_vector(7 downto 0) := (others => '0');
    signal rdata       : std_logic_vector(7 downto 0) := (others => '0');
    signal start_iter  : std_logic := '0';
    signal sid_pwm     : std_logic := '0';
    signal sample_out  : signed(17 downto 0);
    signal stop_clock  : boolean := false;

    signal vc           : real := 0.0;
    constant R          : real := 2200.0;
    constant C          : real := 0.000000022;
begin

    clock <= not clock after 10 ns when not stop_clock; -- 50 MHz
    reset <= '1', '0' after 1 us;

    sid: entity work.sid_top
    generic map (
        g_num_voices  => 7 )
        
    port map (
        clock       => clock,
        reset       => reset,
        
        addr        => addr,
        wren        => wren,
        wdata       => wdata,
        rdata       => rdata,
    
        start_iter  => start_iter,
        sample_out  => sample_out );
    
    i_pdm_sid: entity work.sigma_delta_dac
    generic map (
        g_left_shift => 0,
        g_width => sample_out'length )
    port map (
        clock   => clock,
        reset   => reset,
        
        dac_in  => sample_out,
    
        dac_out => sid_pwm );

    filter: process(clock)
        variable v_dac : real;
        variable i_r   : real;
        variable q_c   : real;
    begin
        if rising_edge(clock) then
            if sid_pwm='0' then
                v_dac := -1.2;
            else
                v_dac := 1.2;
            end if;
            i_r := (v_dac - vc) / R;
            q_c := i_r * 20.0e-9; -- 20 ns;
            vc  <= vc + (q_c / C);
        end if;
    end process;

    test: process
        procedure do_write(a : natural; d : std_logic_vector(7 downto 0)) is
        begin
            wait until clock='1';
            addr    <= to_unsigned(a, 7);
            wdata   <= d;
            wren    <= '1';
            wait until clock='1';
            wren    <= '0';
        end procedure;
    begin
        wait until reset='0';
        wait until clock='1';
        
        do_write(24, X"0F"); -- volume = 15
        -- voice 1
        do_write(0,  X"8A"); -- freq = 1000 Hz
        do_write(1,  X"41"); 
        do_write(2,  X"55"); -- pulse_width = 1/3 (4095 / 3)
        do_write(3,  X"05");
        do_write(5,  X"01"); -- attack = 0, release = 1
        do_write(6,  X"F1"); -- sustain = 8, decay = 1
        do_write(4,  X"11"); -- triangle, gate = 1, with sync

        wait for 4 ms;
        do_write(4,  X"19"); -- triangle with test
        wait for 50 us;
        do_write(4,  X"11"); -- reset test bit
        wait for 50 us;
        do_write(4,  X"01"); -- waveform 0
        wait for 100 us;
        do_write(4,  X"19"); -- triangle with test
        wait for 1 us;
        do_write(4,  X"11"); -- reset test bit
        wait for 20 us;
        do_write(4,  X"01"); -- waveform 0
        

        -- voice 3
        do_write(14+0,  X"FC"); -- freq = 900 Hz
        do_write(14+1,  X"3A"); 
        do_write(14+1,  X"FF"); -- max for ring mod view in wave
        do_write(14+2,  X"55"); -- pulse_width = 1/3 (4095 / 3)
        do_write(14+3,  X"05");
        do_write(14+5,  X"11"); -- attack = 1, release = 1
        do_write(14+6,  X"81"); -- sustain = 8, decay = 1

--        do_write(22, X"20"); -- low frequency filter
--        do_write(23, X"41"); -- a bit of resonance for voice1
--        do_write(24, X"1F"); -- full volume, with LP

--        wait for 20 ms;
--        do_write(24, X"0C"); -- volume = 12
--
--        do_write( 7,  X"22");
--        do_write( 8,  X"39");
--        do_write(12,  X"11");
--        do_write(13,  X"F1");
--        do_write(11,  X"11"); -- saw
--        
--        wait for 20 ms;
--        
--        -- voice 3 oscillator on
--        do_write(14,  X"E1"); -- freq = 1234 Hz
--        do_write(15,  X"50"); 
--        do_write(19,  X"22"); -- attack = 2, release = 2
--        do_write(20,  X"A2"); -- sustain = 10, decay = 2
--        do_write(18,  X"11"); -- select triangle, to see it on the output register

        wait for 50 ms;
        do_write(4,  X"20"); -- gate off
        do_write(18, X"10"); -- gate off
        wait for 70 ms;
        stop_clock <= true;
        wait;
    end process test;

    process
    begin
        wait for 980 ns;
        start_iter <= '1';
        wait for 20 ns;
        start_iter <= '0';
        if stop_clock then
            wait;
        end if;
    end process;
end tb;
