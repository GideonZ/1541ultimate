library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity phase_detector is
port (
    n           : in  unsigned(11 downto 0);
    pll_clock   : in  std_logic;
    h_sync      : in  std_logic;
    mute        : in  std_logic;
    reference   : out std_logic;

    pulse_level : out std_logic;
    pulse_enable: out std_logic;
    up          : out std_logic;
    down        : out std_logic;
    analog      : out std_logic );

end phase_detector;


architecture gideon of phase_detector is
    signal div_count    : unsigned(n'range) := (others => '0');
    signal divided      : std_logic;
    signal h_latch      : std_logic;
    signal ref_latch    : std_logic;
    signal release      : std_logic;
    signal up_i         : std_logic;
    signal down_i       : std_logic;
begin
    reference <= divided;
    
    process(pll_clock)
    begin
        if rising_edge(pll_clock) then
            if div_count = 0 then
                div_count <= n;
                divided <= '1';
            else
                div_count <= div_count - 1;
                if div_count = ('0' & n(11 downto 1)) then
                    divided <= '0';
                end if;
            end if;
        end if;
    end process;

    process(h_sync, release)
    begin
        if release='1' then
            h_latch <= '0';
        elsif rising_edge(h_sync) then
            h_latch <= '1';
        end if;
    end process;

    process(divided, release)
    begin
        if release='1' then
            ref_latch <= '0';
        elsif rising_edge(divided) then
            ref_latch <= '1';
        end if;
    end process;

    up_i    <= h_latch and not ref_latch;
    down_i  <= ref_latch and not h_latch;
    release <= h_latch and ref_latch;

    pulse_enable <= (up_i or down_i) and not mute;
    pulse_level  <= up_i;

    analog <= '0' when (down_i='1' and up_i='0' and mute='0') else
              '1' when (up_i='1' and down_i='0' and mute='0') else
              'Z';

    up   <= up_i;
    down <= down_i;

end gideon;
