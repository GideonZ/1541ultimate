library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;

entity delta_sigma_2to5 is
generic (
    g_width     : positive := 12 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    dac_in      : in  signed(g_width-1 downto 0);
    dac_out     : out std_logic );
end entity;

architecture gideon of delta_sigma_2to5 is
--    signal input        : unsigned(g_width-1 downto 0);
    signal input        : unsigned(15 downto 0);
    signal level        : unsigned(1 downto 0);
    signal modulated    : integer range 0 to 3;
    signal count        : integer range 0 to 4;
    signal out_i        : std_logic;
    signal mash_enable  : std_logic;
    signal sine         : signed(15 downto 0);
begin
    dac_out <= out_i;
    --input   <= not(dac_in(dac_in'high)) & unsigned(dac_in(dac_in'high-1 downto 0));
    input   <= not(sine(sine'high)) & unsigned(sine(sine'high-1 downto 0));
    level   <= to_unsigned(modulated, 2);
    
    i_pilot: entity work.sine_osc
    port map (
        clock   => clock,
        enable  => mash_enable,
        reset   => reset,
        
        sine    => sine,
        cosine  => open );

    i_mash: entity work.mash
    generic map (2, input'length)
    port map (
        clock   => clock,
        enable  => mash_enable,
        reset   => reset,
        
        dac_in  => input,
        dac_out => modulated );

    process(clock)
    begin
        if rising_edge(clock) then
            mash_enable <= '0';
            case count is
            when 0 =>
                out_i <= '0';
            when 1 =>
                if level="11" then
                    out_i <= '1';
                else
                    out_i <= '0';
                end if;
            when 2 =>
                out_i <= level(1);
            when 3 =>
                if level="00" then
                    out_i <= '0';
                else
                    out_i <= '1';
                end if;
            when 4 =>
                mash_enable <= '1';
                out_i <= '1';

            when others =>
                null;
            end case;

            if count = 4 then
                count <= 0;
            else
                count <= count + 1;
            end if;

            if reset='1' then
                out_i <= not out_i;
                count <= 0;
            end if;
        end if;
    end process;
end gideon;
