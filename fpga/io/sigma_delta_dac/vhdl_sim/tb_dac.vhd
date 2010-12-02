library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity tb_dac is
end tb_dac;

architecture tb of tb_dac is
    signal clk, reset   : std_logic := '0';
    signal dac_in       : signed(11 downto 0) := (others => '0');
    signal dac_out_1    : std_logic;
    signal dac_out_2    : std_logic;
    signal vc_1         : real := 0.0;
    signal vc_2         : real := 0.0;
    signal diff         : real := 0.0;
    constant R          : real := 2200.0;
    constant C          : real := 0.000000022;
begin
    clk <= not clk after 10 ns; -- 50 MHz
    reset <= '1', '0' after 100 ns;

    dac1: entity work.sigma_delta_dac
    generic map (12, 0)
    port map (
        clock   => clk,
        reset   => reset,
        
        dac_in  => dac_in,
        dac_out => dac_out_1 );

    dac2: entity work.sigma_delta_dac
    generic map (12, 1)
    port map (
        clock   => clk,
        reset   => reset,
        
        dac_in  => dac_in,
        dac_out => dac_out_2 );

    test:process
    begin
        dac_in <= X"800";
        wait for 1000 us;
        dac_in <= X"A00";
        wait for 1000 us;
        dac_in <= X"C00";
        wait for 1000 us;
        dac_in <= X"E00";
        wait for 1000 us;
        dac_in <= X"000";
        wait for 1000 us;
        dac_in <= X"200";
        wait for 1000 us;
        dac_in <= X"400";
        wait for 1000 us;
        dac_in <= X"600";
        wait for 1000 us;
        dac_in <= X"7FF";
        wait for 1000 us;
--        dac_in <= X"002";
--        wait for 2000 us;
--        dac_in <= X"7FF";
--        wait for 500 us;
--        dac_in <= X"800";
--        wait for 500 us;
        for i in 0 to 15 loop
            dac_in <= to_signed(i * 16#111#, 12);
            wait for 10 us; -- 10 kHz
        end loop;
        for i in 0 to 15 loop
            dac_in <= to_signed(i * 16#111#, 12);
            wait for 20 us; -- 10 kHz
        end loop;

        -- now generate a 2 kHz wave (sample rate = 500 kHz, 250 clocks / sample, 100 samples per sine wave)
        for i in 0 to 400 loop
            dac_in <= to_signed(integer(2000.0 * sin(real(i) / 15.91549430919)), 12);
            wait for 5 us;        
        end loop;

        -- now generate a 5 kHz wave (sample rate = 500 kHz, 100 clocks / sample, 100 samples per sine wave)
        for i in 0 to 1000 loop
            dac_in <= to_signed(integer(2000.0 * sin(real(i) / 15.91549430919)), 12);
            wait for 2 us;        
        end loop;

        -- now generate a 10 kHz wave (sample rate = 500 kHz, 50 clocks / sample, 100 samples per sine wave)
        for i in 0 to 2000 loop
            dac_in <= to_signed(integer(2000.0 * sin(real(i) / 15.91549430919)), 12);
            wait for 1 us;        
        end loop;

        dac_in <= X"7FF";
        wait;
    end process;

    filter: process(clk)
        variable v_dac : real;
        variable i_r   : real;
        variable q_c   : real;
    begin
        if rising_edge(clk) then
            if dac_out_1='0' then
                v_dac := -1.2;
            else
                v_dac := 1.2;
            end if;
            i_r := (v_dac - vc_1) / R;
            q_c := i_r * 20.0e-9; -- 20 ns;
            vc_1  <= vc_1 + (q_c / C);
            -------
            if dac_out_2='0' then
                v_dac := -1.0;
            else
                v_dac := 1.0;
            end if;
            i_r := (v_dac - vc_2) / R;
            q_c := i_r * 20.0e-9; 
            vc_2  <= vc_2 + (q_c / C);
        end if;
    end process;
    diff <= vc_2 - vc_1;
end tb;

