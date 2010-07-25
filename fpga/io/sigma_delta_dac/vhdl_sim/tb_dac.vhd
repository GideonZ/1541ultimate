library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_signed.all;
use ieee.math_real.all;

entity tb_dac is
end tb_dac;

architecture tb of tb_dac is
    signal clk, reset   : std_logic := '0';
    signal dac_in       : std_logic_vector(11 downto 0);
    signal dac_out      : std_logic;
    signal vc           : real := 0.0;

    constant R          : real := 2200.0;
    constant C          : real := 0.00000001;
begin
    clk <= not clk after 10 ns; -- 50 MHz
    reset <= '1', '0' after 100 ns;

    dac: entity work.sigma_delta_dac
    generic map (12)
    port map (
        clk     => clk,
        reset   => reset,
        
        dac_in  => dac_in,
        dac_out => dac_out );

    test:process
    begin
        for i in 0 to 15 loop
            dac_in <= conv_std_logic_vector(i * 16#111#, 12);
            wait for 100 us; -- 10 kHz
        end loop;
        for i in 0 to 15 loop
            dac_in <= conv_std_logic_vector(i * 16#111#, 12);
            wait for 200 us; -- 10 kHz
        end loop;

        -- now generate a 2 kHz wave (sample rate = 500 kHz, 250 clocks / sample, 100 samples per sine wave)
        for i in 0 to 400 loop
            dac_in <= conv_std_logic_vector(integer(2000.0 + 2000.0 * sin(real(i) / 15.91549430919)), 12);
            wait for 5 us;        
        end loop;

        -- now generate a 5 kHz wave (sample rate = 500 kHz, 100 clocks / sample, 100 samples per sine wave)
        for i in 0 to 1000 loop
            dac_in <= conv_std_logic_vector(integer(2000.0 + 2000.0 * sin(real(i) / 15.91549430919)), 12);
            wait for 2 us;        
        end loop;

        -- now generate a 10 kHz wave (sample rate = 500 kHz, 50 clocks / sample, 100 samples per sine wave)
        for i in 0 to 2000 loop
            dac_in <= conv_std_logic_vector(integer(2000.0 + 2000.0 * sin(real(i) / 15.91549430919)), 12);
            wait for 1 us;        
        end loop;

        dac_in <= X"800";
        wait;
    end process;

    filter: process(clk)
        variable v_dac : real;
        variable i_r   : real;
        variable q_c   : real;
    begin
        if rising_edge(clk) then
            if dac_out='0' then
                v_dac := 0.0;
            else
                v_dac := 3.3;
            end if;
            i_r := (v_dac - vc) / R;
            q_c := i_r * 20.0e-9; -- 20 ns;
            vc  <= vc + (q_c / C);
        end if;
    end process;
end tb;

