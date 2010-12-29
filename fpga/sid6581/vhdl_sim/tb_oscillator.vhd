
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity tb_oscillator is
end tb_oscillator;

architecture TB of tb_oscillator is

    constant num_voices : integer := 8;
    signal clk      : std_logic := '0';
    signal reset    : std_logic;
    signal freq     : std_logic_vector(15 downto 0);
    signal osc_val  : std_logic_vector(23 downto 0);
    signal voice_i  : std_logic_vector(3 downto 0);
    signal voice_o  : std_logic_vector(3 downto 0);
    signal voice_m  : std_logic_vector(3 downto 0);
    signal carry_20 : std_logic;
    signal wave_out : std_logic_vector(7 downto 0);
    signal wave_sel : std_logic_vector(3 downto 0);

    type freq_array_t is array(natural range <>) of std_logic_vector(15 downto 0);
    constant freq_array : freq_array_t(0 to 15) := ( X"A001", X"B003", X"B805", X"C000",
                                                     X"E00B", X"F00D", X"7011", X"FFFF",
                                                     X"0017", X"001D", X"0100", X"0200",
                                                     X"0400", X"0800", X"1000", X"2000" );

    type sel_array_t is array(natural range <>) of std_logic_vector(3 downto 0);
    constant sel_array : sel_array_t(0 to 15) := ( X"1", X"2", X"4", X"8", X"3", X"6", X"C", X"8",
                                                   X"9", X"7", X"E", X"D", X"B", X"5", X"A", X"F" );

    type wave_array_t is array(natural range <>) of std_logic_vector(7 downto 0);
    signal wave_mem : wave_array_t (0 to num_voices-1) := (others => (others => '0'));
begin
    clk <= not clk after 15 ns;
    reset <= '1', '0' after 600 ns;    
    
    osc: entity work.oscillator
    generic map (num_voices)
    port map (
        clk      => clk,
        reset    => reset,
        freq     => freq,
        voice_i  => voice_i,
        voice_o  => voice_o,
        osc_val  => osc_val,
        carry_20 => carry_20 );

    process(clk)
    begin
        if rising_edge(clk) then
            if reset='1' then
                voice_i <= X"0";
            elsif voice_i = num_voices-1 then
                voice_i <= X"0";
            else
                voice_i <= voice_i + 1;
            end if;
        end if;
    end process;

    wmap: entity work.wave_map
    generic map (
        num_voices => 8,  -- 8 or 16, clock should then be 8 or 16 MHz, too!
        sample_bits=> 8 )
    port map (
        clk      => clk,
        reset    => reset,
        
        osc_val  => osc_val,
        carry_20 => carry_20,
        
        voice_i  => voice_o,
        wave_sel => wave_sel,
        sq_width => X"27D",
    
        voice_o  => voice_m,
        wave_out => wave_out );
    
    freq     <= freq_array(conv_integer(voice_i));
    wave_sel <= sel_array(conv_integer(voice_o));
    
    process(clk)
    begin
        if rising_edge(clk) then
            wave_mem(conv_integer(voice_m)) <= wave_out;
        end if;
    end process;
    
end TB;
