--------------------------------------------------------------------------------
-- Entity: fractional_div
-- Date:2018-08-18  
-- Author: gideon     
--
-- Description: Fractional Divider
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fractional_div is
generic (
    g_accu_bits     : natural :=   8;
    g_numerator     : natural :=   3;
    g_denominator   : natural := 200 );
port (
    clock       : in  std_logic;
    tick        : out std_logic := '0'; -- this should yield a 16 MHz tick
    tick_by_4   : out std_logic; -- this should yield a 4 MHz tick (for drive logic)
    tick_by_16  : out std_logic; -- this should yield an 1 MHz tick (i.e. for IEC processor)
    one_16000   : out std_logic ); -- and thus, this should yield a 1 ms tick
end entity;

architecture arch of fractional_div is
    constant c_min  : integer := -g_numerator;
    constant c_max  : integer :=  g_denominator - g_numerator - 1;

    function log2_ceil(arg: integer) return natural is
        variable v_temp   : integer;
        variable v_result : natural;
    begin
        v_result    := 0;
        v_temp      := arg / 2;
        while v_temp /= 0 loop
            v_temp      := v_temp / 2;
            v_result    := v_result + 1;
        end loop;
        if 2**v_result < arg then
            v_result := v_result + 1;
        end if;
        assert false report "Ceil: " & integer'image(arg) & " result: " & integer'image(v_result) severity warning;
        return v_result;
    end function;
    
    function maxof2(a, b : integer) return integer is
        variable res : integer;
    begin
        res := b;
        assert false report "Max: " & integer'image(a) & " and " & integer'image(b) severity warning;
        if a > b then
            res := a;
        end if;
        return res;
    end function;
    
    constant c_bits_min    : integer := 1 + log2_ceil(-c_min - 1);
    constant c_bits_max    : integer := 1 + log2_ceil(c_max);
    constant c_bits_needed : integer := maxof2(c_bits_min, c_bits_max);
    signal accu      : signed(g_accu_bits downto 0) := (others => '0'); 
    signal div16000  : unsigned(13 downto 0) := to_unsigned(128, 14);
    
begin
    assert c_bits_max <= accu'length report "Need more bits for accu (max:"&integer'image(c_max)&", but Xilinx doesn't let me define this dynamically:" & integer'image(c_bits_max) severity error;
    assert c_bits_min <= accu'length report "Need more bits for accu (min:"&integer'image(c_min)&", but Xilinx doesn't let me define this dynamically:" & integer'image(c_bits_min) severity error;
    assert c_bits_needed <= accu'length report "Need more bits for accu ("&integer'image(c_bits_needed)&"/"&integer'image(accu'length)&", but Xilinx doesn't let me define this dynamically" severity error;

    process(clock)
    begin
        if rising_edge(clock) then
            tick <= '0';
            one_16000 <= '0';
            tick_by_4 <= '0';
            tick_by_16 <= '0';
            if accu(accu'high) = '1' then
                if div16000 = 0 then
                    one_16000 <= '1';
                    div16000 <= to_unsigned(15999, 14);
                else
                    div16000 <= div16000 - 1;
                end if;
                if div16000(3 downto 0) = "0001" then
                    tick_by_16 <= '1';
                end if;
                if div16000(1 downto 0) = "01" then
                    tick_by_4 <= '1';
                end if;
                tick <= '1';
                accu <= accu + (g_denominator - g_numerator);
            else
                accu <= accu - g_numerator;
            end if;
        end if;
    end process;    
end architecture;
