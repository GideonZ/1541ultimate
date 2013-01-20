library ieee;
use ieee.std_logic_1164.all;

entity oc_pusher is
port (
    clock   : in  std_logic;
    sig_in  : in  std_logic;
    oc_out  : out std_logic );
end entity;

architecture gideon of oc_pusher is
    signal sig_d    : std_logic;
    signal drive    : std_logic;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            sig_d <= sig_in;
        end if;
    end process;

    drive  <= not sig_in or not sig_d;
    oc_out <= sig_in when drive='1' else 'Z';

end gideon;
    