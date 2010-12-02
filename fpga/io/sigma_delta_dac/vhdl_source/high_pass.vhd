library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;

entity high_pass is
generic (
    g_width         : natural := 12 );
port (
    clock           : in  std_logic;
    enable          : in  std_logic := '1';
    reset           : in  std_logic;
    
    x               : in  signed(g_width-1 downto 0);
    q               : out signed(g_width downto 0) );
end high_pass;

architecture gideon of high_pass is
    signal z        : signed(g_width downto 0);
    signal count    : integer range 0 to 3;
begin
    process(clock)
        variable z_half : signed(g_width downto 0);
    begin
        if rising_edge(clock) then
            z_half := (z(z'high) & z(z'high downto 1));
            if enable='1' then
                z <= sum_limit((x(x'high) & x), z_half); -- z=x+(z/2)
                q <= sub_limit((x(x'high) & x), z_half); -- q=x-(z/2)
            end if;
            
--            z <= extend(x, g_width+1);
--            q <= extend(x, g_width+1) - z;

            if reset='1' then
                z <= (others => '0');
                q <= (others => '0');
                count <= 0;
            end if;
        end if;
    end process;
end gideon;
