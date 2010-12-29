-------------------------------------------------------------------------------
-- Date       $Date: 2005/04/12 19:09:27 $
-- Author     $Author: Gideon $
-- Revision   $Revision: 1.1 $
-- Log        $Log: oscillator.vhd,v $
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.tl_flat_memory_model_pkg.all;
use work.wave_pkg.all;

entity tb_sid_from_file is

end tb_sid_from_file;

architecture tb of tb_sid_from_file is

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
    constant c_cpu_period : time := 1014973 ps;
    constant c_half_clock : time := c_cpu_period / 8;
    constant c_clock      : time := c_half_clock * 2;
    
begin

    clock <= not clock after c_half_clock when not stop_clock; -- 5 MHz
    reset <= '1', '0' after 1 us;

    sid: entity work.sid_top
    generic map (
        g_filter_div  => 20,  -- 194 for 50 MHz; 
        g_num_voices  => 3 )
        
    port map (
        clock       => clock,
        reset       => reset,
        
        addr        => addr,
        wren        => wren,
        wdata       => wdata,
        rdata       => rdata,
    
        start_iter  => start_iter,
        sample_out  => sample_out );
    
--    i_pdm_sid: entity work.sigma_delta_dac
--    generic map (
--        g_left_shift => 0,
--        g_width => sample_out'length )
--    port map (
--        clock   => clock,
--        reset   => reset,
--        
--        dac_in  => sample_out,
--    
--        dac_out => sid_pwm );
--
--    filter: process(clock)
--        variable v_dac : real;
--        variable i_r   : real;
--        variable q_c   : real;
--    begin
--        if rising_edge(clock) then
--            if sid_pwm='0' then
--                v_dac := -1.2;
--            else
--                v_dac := 1.2;
--            end if;
--            i_r := (v_dac - vc) / R;
--            q_c := i_r * 200.0e-9; -- 200 ns;
--            vc  <= vc + (q_c / C);
--        end if;
--    end process;
--
    test: process
        variable trace_in  : h_mem_object;
        variable addr_i    : integer := 0;
        variable delay     : time := 1 ns;
        variable t         : unsigned(23 downto 0);
        variable b         : std_logic_vector(7 downto 0);
        
    begin
        register_mem_model(tb_sid_from_file'path_name, "trace", trace_in);
        load_memory("sidtrace.555", trace_in, X"00000000");
        
        wait until reset='0';
        wait until clock='1';
        
        L1: while now < 5000 ms loop
            b := read_memory_8(trace_in, std_logic_vector(to_unsigned(addr_i, 32)));
            addr  <= unsigned(b(6 downto 0));
            wdata <= read_memory_8(trace_in, std_logic_vector(to_unsigned(addr_i+1, 32)));
            t(23 downto 16) := unsigned(read_memory_8(trace_in, std_logic_vector(to_unsigned(addr_i+2, 32))));
            t(15 downto  8) := unsigned(read_memory_8(trace_in, std_logic_vector(to_unsigned(addr_i+3, 32))));
            t( 7 downto  0) := unsigned(read_memory_8(trace_in, std_logic_vector(to_unsigned(addr_i+4, 32))));
            addr_i := addr_i + 5;
            if t = 0 then
                exit L1;
            end if;
            delay := (1 us) * (to_integer(t)-1);
            wait for delay;
            wren <= '1';
            wait for 1 * c_clock;
            wren <= '0';
            wait for 3 * c_clock;
        end loop; 

        stop_clock <= true;
        wait;
    end process test;
    
    process
    begin
        wait for 3 * c_clock;
        start_iter <= '1';
        wait for 1 * c_clock;
        start_iter <= '0';
        if stop_clock then
            wait;
        end if;
    end process;

    -- this clock is a little faster (1 Mhz instead of 985250 Hz, thus we need to adjust our sample rate by the same amount
    -- which means we need to sample at 48718 Hz instead of 48kHz. Sample period = 
    process
        variable chan   : t_wave_channel;
    begin
        open_channel(chan);
        while not stop_clock loop
            wait for 20833 ns; --  20526 ns;
            push_sample(chan, to_integer(sample_out(17 downto 2)));
        end loop;
        write_wave("sid_wave.wav", 48000, (0 => chan));
        wait;
    end process;

end tb;
