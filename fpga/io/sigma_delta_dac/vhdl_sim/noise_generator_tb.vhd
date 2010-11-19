library ieee;
use ieee.std_logic_1164.all;

entity noise_generator_tb is

end;

architecture tb of noise_generator_tb is
    constant c_type        : string := "Fibonacci";
    constant c_polynom     : std_logic_vector := X"E10000";

--    constant c_type        : string := "Galois";
--    constant c_polynom     : std_logic_vector := X"5D6DCB";

    constant c_seed        : std_logic_vector := X"000001";
    
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal q               : std_logic_vector(c_polynom'length-1 downto 0);
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;        

    i_lfsr: entity work.noise_generator
    generic map (
        g_type          => c_type,
        g_polynom       => c_polynom,
        g_seed          => c_seed )
    port map (
        clock           => clock,
        reset           => reset,
        
        q               => q );
        
    p_test: process(clock)
        variable count : integer;
    begin
        if rising_edge(clock) then
            if reset='1' then
                count := 0;
            else
                assert count=0 or q/=c_seed
                    report "Length of LFSR = " & integer'image(count)
                    severity failure;
                count := count + 1;
            end if;
        end if;
    end process;    
        
end tb;
