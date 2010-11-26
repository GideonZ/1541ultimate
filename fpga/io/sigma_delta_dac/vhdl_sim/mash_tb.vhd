library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity mash_tb is
end mash_tb;

architecture tb of mash_tb is
    signal clock        : std_logic := '0';
    signal reset        : std_logic := '0';
    signal dac_in       : signed(11 downto 0);
    signal dac_in_u     : unsigned(11 downto 0);
    signal dac_out      : integer;
    signal vc           : real := 0.0;
    signal min_out      : integer := 10000;
    signal max_out      : integer := -10000;
    
    constant R          : real := 2200.0;
    constant C          : real := 0.00000001;
begin
    clock <= not clock after 10 ns; -- 50 MHz
    reset <= '1', '0' after 100 ns;

    dac_in_u(dac_in_u'high) <= not dac_in(dac_in'high);
    dac_in_u(dac_in_u'high-1 downto 0) <= unsigned(dac_in(dac_in'high-1 downto 0));
    
    dac: entity work.mash
    generic map (4, 12)
    port map (
        clock     => clock,
        reset   => reset,
        
        dac_in  => dac_in_u,
        dac_out => dac_out );

    test:process
    begin
        dac_in <= X"000";
        wait for 100 us;
        for i in 0 to 9 loop
            wait until clock='1';
            dac_in <= X"7FF";
            wait until clock='1';
            dac_in <= X"800";
        end loop;        

        dac_in <= X"001";
        wait for 100 us;
        dac_in <= X"002";
        wait for 100 us;
        dac_in <= X"003";
        wait for 100 us;
        dac_in <= X"011";
        wait for 100 us;
        dac_in <= X"022";
        wait for 100 us;
        dac_in <= X"033";
        wait for 100 us;
        dac_in <= X"100";
        wait for 100 us;
        dac_in <= X"200";
        wait for 100 us;
        dac_in <= X"400";
        wait for 100 us;
        dac_in <= X"7FF";
        wait for 100 us;
        dac_in <= X"800";
        wait for 100 us;
        dac_in <= X"C00";
        wait for 100 us;
        dac_in <= X"E00";
        wait for 100 us;
        dac_in <= X"F00";
        wait for 100 us;
        dac_in <= X"F80";
        wait for 100 us;
        dac_in <= X"FC0";
        wait for 100 us;
        dac_in <= X"FE0";
        wait for 100 us;
        dac_in <= X"FF0";
        wait for 100 us;
        dac_in <= X"FF8";
        wait for 100 us;
        dac_in <= X"FFC";
        wait for 100 us;
        dac_in <= X"FFE";
        wait for 100 us;
        dac_in <= X"FFF";
        wait for 100 us;

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

        dac_in <= X"000";
        wait;
    end process;

    filter: process(clock)
        variable v_dac : real;
        variable i_r   : real;
        variable q_c   : real;
    begin
        if rising_edge(clock) then
            if dac_out > max_out then
                max_out <= dac_out;
            end if;
            if dac_out < min_out then
                min_out <= dac_out;
            end if;

--            if dac_out='0' then
--                v_dac := 0.0;
--            else
--                v_dac := 3.3;
--            end if;
            v_dac := real(dac_out);
            i_r := (v_dac - vc) / R;
            q_c := i_r * 20.0e-9; -- 20 ns;
            vc  <= vc + (q_c / C);
        end if;
    end process;
end tb;

