library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity high_pass is
generic (
    g_width         : natural := 12 );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    x               : in  signed(g_width-1 downto 0);
    q               : out signed(g_width-1 downto 0) );
end high_pass;

architecture gideon of high_pass is
    signal z        : signed(g_width-1 downto 0);
begin
    process(clock)
    begin
        if rising_edge(clock) then
            z <= x + (z(z'high) & z(z'high downto 1)); -- z=x+(z/2)
            q <= x - (z(z'high) & z(z'high downto 1)); -- q=x-(z/2)
            
            if reset='1' then
                z <= (others => '0');
                q <= (others => '0');
            end if;
        end if;
    end process;
end gideon;
