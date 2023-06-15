--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: Package with ALU related functions.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package alu_pkg is

    function sign_extend(value: std_logic_vector; fill: std_logic; size: positive) return std_logic_vector;
    function shift_left(value : std_logic_vector(31 downto 0); shamt : std_logic_vector(4 downto 0)) return std_logic_vector;
    function shift_right(value : std_logic_vector(31 downto 0); shamt : std_logic_vector(4 downto 0); padding: std_logic) return std_logic_vector;

end package;

package body alu_pkg IS

    function shift_left(value: std_logic_vector(31 downto 0); shamt: std_logic_vector(4 downto 0)) return std_logic_vector is
        variable result: std_logic_vector(31 downto 0);
        variable paddings: std_logic_vector(15 downto 0);
    begin
        paddings := (others => '0');
        result := value;
        if (shamt(4) = '1') then result := result(15 downto 0) & paddings(15 downto 0); end if;
        if (shamt(3) = '1') then result := result(23 downto 0) & paddings( 7 downto 0); end if;
        if (shamt(2) = '1') then result := result(27 downto 0) & paddings( 3 downto 0); end if;
        if (shamt(1) = '1') then result := result(29 downto 0) & paddings( 1 downto 0); end if;
        if (shamt(0) = '1') then result := result(30 downto 0) & paddings( 0 );         end if;
        return result;
    end function;

    function shift_right(value: std_logic_vector(31 downto 0); shamt: std_logic_vector(4 downto 0); padding: std_logic) return std_logic_vector is
        variable result: std_logic_vector(31 downto 0);
        variable paddings: std_logic_vector(15 downto 0);
    begin
        paddings := (others => padding);
        result := value;
        if (shamt(4) = '1') then result := paddings(15 downto 0) & result(31 downto 16); end if;
        if (shamt(3) = '1') then result := paddings( 7 downto 0) & result(31 downto  8); end if;
        if (shamt(2) = '1') then result := paddings( 3 downto 0) & result(31 downto  4); end if;
        if (shamt(1) = '1') then result := paddings( 1 downto 0) & result(31 downto  2); end if;
        if (shamt(0) = '1') then result := paddings( 0 )         & result(31 downto  1); end if;
        return result;
    end function;

    function sign_extend(value: std_logic_vector; fill: std_logic; size: positive) return std_logic_vector is
        variable a: std_logic_vector (size - 1 downto 0);
    begin
        a(size - 1 downto value'length) := (others => fill);
        a(value'length - 1 downto 0) := value;
        return a;
    end function;

end package body;
