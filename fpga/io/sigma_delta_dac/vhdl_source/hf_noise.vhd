library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity hf_noise is
generic (
    g_hp_filter     : boolean := true );
port (
    clock           : in  std_logic;
    enable          : in  std_logic;
    reset           : in  std_logic;
    
    q               : out signed(15 downto 0) );

end entity;

architecture structural of hf_noise is
--    constant c_type        : string := "Galois";
--    constant c_polynom     : std_logic_vector := X"5D6DCB";
    constant c_type        : string := "Fibonacci";
    constant c_polynom     : std_logic_vector := X"E10000";
    constant c_seed        : std_logic_vector := X"000001";
    signal raw_lfsr        : std_logic_vector(c_polynom'length-1 downto 0);
    signal noise           : signed(15 downto 0);
    signal hp              : signed(15 downto 0);
begin
    i_lfsr: entity work.noise_generator
    generic map (
        g_type          => c_type,
        g_polynom       => c_polynom,
        g_seed          => c_seed )
    port map (
        clock           => clock,
        enable          => enable,
        reset           => reset,
        
        q               => raw_lfsr );
        
--    Reordering the bits somehow randomly,
--    gives a better spectral distribution
--    in case of Galois (flat!)! In face of Fibonacci,
--    this reordering gives ~20dB dips at odd
--    locations, depending on the reorder choice.

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

    -- taking an up-range or down range gives a reasonably
    -- flat frequency response, but lacks low frequencies (~20 dB)
    -- In case of our high-freq noise that we need, this is not
    -- a bad thing, as our filter doesn't need to be so sharp.
    
    -- Fibonacci gives less low frequency noise than Galois!
    -- differences seen with 1st order filter below were 5-10dB in
    -- our band of interest.
    
--    noise     <= signed(raw_lfsr(20 downto 5));

    r_hp: if g_hp_filter generate
        signal hp_a : signed(15 downto 0);
    begin
        i_hp: entity work.high_pass
        generic map (
            g_width         => 15 )
        port map (
            clock           => clock,
            enable          => enable,
            reset           => reset,
            
            x               => noise(14 downto 0),
--           q               => hp_a );

--        i_hp_b: entity work.high_pass
--        generic map (
--            g_width         => 16 )
--        port map (
--            clock           => clock,
--            reset           => reset,
--            
--            x               => hp_a,
            q               => hp );

    end generate;

    q <= hp(15 downto 0) when g_hp_filter else noise;
        
end structural;
