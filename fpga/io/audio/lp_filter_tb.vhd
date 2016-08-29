-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : lp_filter_tb
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Testbench for low pass filter
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use ieee.math_real.all;
    
entity lp_filter_tb is

end entity;

architecture test of lp_filter_tb is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal signal_in   : signed(17 downto 0);
    signal high_pass   : signed(17 downto 0);
    signal band_pass   : signed(17 downto 0);
    signal low_pass    : signed(17 downto 0);
    signal valid_out   : std_logic;
    signal s_freq      : real := 625.0; -- Hz
    signal sin_step    : real := 0.0;
    signal max         : signed(17 downto 0);
begin
    clock <= not clock after  8 ns; -- 62.5 MHz
    reset <= '1', '0' after 100 ns;
    
    sin_step <= (16.0e-9 * s_freq * 2.0 * 3.14159265);

    process
    begin
        while (s_freq < 100000.0) loop
            max <= to_signed(0, max'length);
            for i in 0 to 250000-1 loop
                signal_in <= to_signed(integer(65536.0 * sin(sin_step * real(i))), 18);
                wait until clock = '1';
                if (i > 50000) and (low_pass > max) then
                    max <= low_pass;
                end if;
            end loop;
            report real'image(s_freq) & "," & real'image(real(to_integer(max)) / 65536.0);
            s_freq <= s_freq * sqrt(2.0);
        end loop;
        wait;        
    end process;

    i_filt: entity work.lp_filter
    port map (
        clock     => clock,
        reset     => reset,
        signal_in => signal_in,
        high_pass => high_pass,
        band_pass => band_pass,
        low_pass  => low_pass,
        valid_out => valid_out
    );

end architecture;
