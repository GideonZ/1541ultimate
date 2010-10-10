library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package itu_pkg is

    constant c_itu_irq_global    : unsigned(3 downto 0) := X"0";
    constant c_itu_irq_enable    : unsigned(3 downto 0) := X"1";
    constant c_itu_irq_disable   : unsigned(3 downto 0) := X"2";
    constant c_itu_irq_edge      : unsigned(3 downto 0) := X"3";
    constant c_itu_irq_clear     : unsigned(3 downto 0) := X"4";
    constant c_itu_irq_active    : unsigned(3 downto 0) := X"5";
    constant c_itu_timer         : unsigned(3 downto 0) := X"6";
    constant c_itu_irq_timer_en  : unsigned(3 downto 0) := X"7";
    constant c_itu_irq_timer_hi  : unsigned(3 downto 0) := X"8"; -- big endian word
    constant c_itu_irq_timer_lo  : unsigned(3 downto 0) := X"9";

	constant c_itu_fpga_version  : unsigned(3 downto 0) := X"B";
    constant c_itu_capabilities0 : unsigned(3 downto 0) := X"C";
    constant c_itu_capabilities1 : unsigned(3 downto 0) := X"D";
    constant c_itu_capabilities2 : unsigned(3 downto 0) := X"E";
    constant c_itu_capabilities3 : unsigned(3 downto 0) := X"F";
	
end package;
