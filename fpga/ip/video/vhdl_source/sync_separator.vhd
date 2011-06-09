library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sync_separator is
generic (
    g_clock_mhz : natural := 50 );
port (
    clock       : in  std_logic;
    
    sync_in     : in  std_logic;

    mute        : out std_logic;
    
    h_sync      : out std_logic;
    v_sync      : out std_logic );
end sync_separator;


architecture low_level of sync_separator is
    constant c_delay    : integer := 50;
    signal h_sync_i     : std_logic := '0';
    signal release      : std_logic := '0';
    signal sync_c       : std_logic := '0';
    signal sync_d1      : std_logic := '0';
    signal sync_d2      : std_logic := '0';
    signal v_sync_pre   : std_logic := '0';
    signal delay        : integer range 0 to c_delay * g_clock_mhz := 0;

    signal raw_c        : std_logic;
    signal raw_d1       : std_logic;
    signal raw_d2       : std_logic;

    signal line_count   : unsigned(8 downto 0) := (others => '0');
begin
    h_sync <= h_sync_i;
    
    process(sync_in, release)
    begin
        if release='1' then
            h_sync_i <= '0';
        elsif falling_edge(sync_in) then
            h_sync_i <= '1';
        end if;
    end process;
    
    process(clock)
    begin
        if rising_edge(clock) then
            sync_c  <= h_sync_i;
            sync_d1 <= sync_c;
            sync_d2 <= sync_d1;
            
--            raw_c  <= sync_in;
--            raw_d1 <= raw_c;
--            raw_d2 <= raw_d1;
--
--            if raw_d1='0' and raw_d2='1' then -- falling edge
--                if delay /= 0 then
--                    mute <= '1';
--                else
--                    mute <= '0';
--                end if;
--            end if;

            if (line_count < 4) or (line_count > 305) then
                mute <= '1';
            else
                mute <= '0';
            end if;

            release <= '0';
            if sync_d1='1' and sync_d2='0' then -- rising edge
                delay <= c_delay * g_clock_mhz;
                if v_sync_pre = '1' then
                    line_count <= (others => '0');
                else
                    line_count <= line_count + 1;
                end if;
            elsif delay /= 0 then
                delay <= delay - 1;
            end if;
            if delay = 1 then            
                v_sync_pre <= not sync_in; -- sample
                release    <= '1';
            end if;
        end if;
    end process;
    v_sync <= v_sync_pre;
    
end low_level;
    