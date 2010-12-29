library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.tl_string_util_pkg.all;

entity noise_generator_tb is

end;

architecture tb of noise_generator_tb is
    constant c_type        : string := "Fibonacci";
--    constant c_polynom     : std_logic_vector := X"E10000";
    constant c_polynom     : std_logic_vector := "11100100000000000000000";
    signal polynom         : std_logic_vector(22 downto 0) := c_polynom;
--    constant c_type        : string := "Galois";
--    constant c_polynom     : std_logic_vector := X"5D6DCB";

--    constant c_seed        : std_logic_vector := X"000001";
    constant c_seed        : std_logic_vector := "11111111111111111111111";
    
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal q               : std_logic_vector(c_polynom'length-1 downto 0);
    signal selected_bits   : std_logic_vector(7 downto 0);
begin
    clock <= not clock after 10 ns;
    --reset <= '1', '0' after 100 ns;        

    i_lfsr: entity work.noise_generator
    generic map (
        g_type          => c_type,
        g_polynom       => c_polynom,
        g_fixed_polynom => false,
        g_seed          => c_seed )
    port map (
        clock           => clock,
        reset           => reset,
        enable          => '1',
        polynom         => polynom,
        
        q               => q );
        
    selected_bits(7) <= q(22);
    selected_bits(6) <= q(20);
    selected_bits(5) <= q(16);
    selected_bits(4) <= q(13);
    selected_bits(3) <= q(11);
    selected_bits(2) <= q(7);
    selected_bits(1) <= q(4);
    selected_bits(0) <= q(2);

    p_test: process
        variable poly  : std_logic_vector(7 downto 0);
        variable count : integer;

        function count_bits(p : std_logic_vector) return integer is
            variable c : integer;
        begin
            c := 0;
            for i in p'range loop
                if p(i)='1' then
                    c := c + 1;
                end if;
            end loop;
            return c;
        end function;

        procedure perform_test is
        begin
            reset <= '1';
            wait until clock='1';
            wait until clock='1';
            wait until clock='1';
            reset <= '0';
            wait until clock='1';
            wait until clock='1';
            count := 1;
            while q /= c_seed loop
                wait until clock='1';
                count := count + 1;
            end loop;
            report "Polynom = " & hstr(polynom) & ". Length of LFSR = " & integer'image(count) severity note;
        end procedure;
    begin
        reset <= '0';
        for i in 0 to 255 loop -- test 256 different polynoms, at least; ones that result in an even number of bits set
            poly := std_logic_vector(to_unsigned(i, 8));
            if(count_bits(poly) mod 2) = 1 then
                polynom <= "1" & poly & "00000000000000";
                perform_test;
            end if;
        end loop;
        wait;
    end process;

--                assert count=0 or q/=c_seed
--                    report "Length of LFSR = " & integer'image(count)
--                    severity failure;
--                count := count + 1;
--            end if;
--        end if;
--    end process;    
--        
end tb;

-- FF 11111111111111111111111
-- FF 11111111111111111111110
-- FF 11111111111111111111100
-- FE 11111111111111111111000 2
-- FE 11111111111111111110000
-- FC 11111111111111111100000 4
-- FC 11111111111111111000000
-- FC 11111111111111110000000
-- F8 11111111111111100000000 7
-- F8 11111111111111000000000
-- F8 11111111111110000000000
-- F8 11111111111100000000000
-- F0 11111111111000000000000 11
-- F0 11111111110000000000000
-- E0 11111111100000000000000 13
-- E0 11111111000000000000000 
-- E0 11111110000000000000000 
-- C0 11111100000000000000000 16
-- C0 1111100000000000000000-
-- C0 11110000000000000000001
-- C0 1110000000000000000001-
-- 81 110000000000000000001-- 20
-- 81 10000000000000000001---
-- 03 000000000000000000----- 22
-- 03
-- 06
-- 06
-- 06
-- 
-- 
-- 