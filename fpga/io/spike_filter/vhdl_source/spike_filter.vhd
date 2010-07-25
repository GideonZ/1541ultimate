library ieee;
    use ieee.std_logic_1164.all;

entity spike_filter is
generic (
    g_stable_time   : integer := 8 );
port (
    clock       : in  std_logic;
    
    pin_in      : in  std_logic;
    pin_out     : out std_logic := '1' );
end spike_filter;

architecture gideon of spike_filter is
    signal pin_c   : std_logic;
    signal cnt     : integer range 0 to g_stable_time-1 := (g_stable_time-1) / 2;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            pin_c <= pin_in;
            if pin_c='0' then
                if cnt = 0 then
                    pin_out <= '0';
                else
                    cnt <= cnt - 1;
                end if;
            else
                if cnt = g_stable_time-1 then
                    pin_out <= '1';
                else
                    cnt <= cnt + 1;
                end if;
            end if;
        end if;
    end process;
end gideon;
