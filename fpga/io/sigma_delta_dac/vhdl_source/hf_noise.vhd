library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity hf_noise is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    q               : out signed(15 downto 0) );

end entity;

architecture structural of hf_noise is
    constant c_type        : string := "Fibonacci";
    constant c_polynom     : std_logic_vector := X"E10000";
    constant c_seed        : std_logic_vector := X"000001";
    signal raw_lfsr        : std_logic_vector(c_polynom'length-1 downto 0);
    signal noise           : signed(15 downto 0);
begin
    i_lfsr: entity work.noise_generator
    generic map (
        g_type          => c_type,
        g_polynom       => c_polynom,
        g_seed          => c_seed )
    port map (
        clock           => clock,
        reset           => reset,
        
        q               => raw_lfsr );
        
    noise(15) <= raw_lfsr(1);
    noise(14) <= raw_lfsr(4);
    noise(13) <= raw_lfsr(7);
    noise(12) <= raw_lfsr(10);
    noise(11) <= raw_lfsr(13);
    noise(10) <= raw_lfsr(16);
    noise(09) <= raw_lfsr(19);
    noise(08) <= raw_lfsr(22);
    noise(07) <= raw_lfsr(20);
    noise(06) <= raw_lfsr(17);
    noise(05) <= raw_lfsr(14);
    noise(04) <= raw_lfsr(11);
    noise(03) <= raw_lfsr(8);
    noise(02) <= raw_lfsr(5);
    noise(01) <= raw_lfsr(2);
    noise(00) <= raw_lfsr(18);


    i_hp: entity work.high_pass
    generic map (
        g_width         => 16 )
    port map (
        clock           => clock,
        reset           => reset,
        
        x               => noise,
        q               => q );

        
end structural;
