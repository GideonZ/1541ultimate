library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sine_osc_tb is
end sine_osc_tb;

architecture tb of sine_osc_tb is

    signal clock   : std_logic := '0';
    signal reset   : std_logic;
    signal sine    : signed(15 downto 0);
    signal cosine  : signed(15 downto 0);

begin

    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    osc: entity work.sine_osc
    port map (
        clock   => clock,
        reset   => reset,
        
        sine    => sine,
        cosine  => cosine );

    process
        variable n: time;
        variable p: integer;
    begin
        wait until reset='0';
        n := now;
        while true loop
            wait until sine(15)='1';
            p := (now - n) / 20 ns;
            n := now;
            report "Period: " & integer'image(p) & " samples" severity note;
        end loop;
    end process;                
        

end tb;