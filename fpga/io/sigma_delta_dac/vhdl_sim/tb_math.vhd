library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.my_math_pkg.all;

entity tb_math is

end tb_math;

architecture tb of tb_math is
    signal i1   : signed(3 downto 0);
    signal i2   : signed(3 downto 0);
    signal sum  : signed(3 downto 0);
    signal diff : signed(3 downto 0);
    signal sum_i: integer;
    signal diff_i: integer;
begin
    process
        variable k,p: signed(3 downto 0);
    begin
        for i in -8 to 7 loop
            for j in -8 to 7 loop
                k := to_signed(i, 4);
                p := to_signed(j, 4);
                sum <= sum_limit(k, p);
                diff <= sub_limit(k, p);
                i1 <= k;
                i2 <= p;
                sum_i <= i + j;
                diff_i <= i - j;
                wait for 1 us;
            end loop;
        end loop;
        wait;
    end process;
end tb;
