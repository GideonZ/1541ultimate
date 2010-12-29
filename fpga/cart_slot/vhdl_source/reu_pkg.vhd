
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
package reu_pkg is

    constant c_status     : unsigned(4 downto 0) := '0' & X"0";
    constant c_command    : unsigned(4 downto 0) := '0' & X"1";
    constant c_c64base_l  : unsigned(4 downto 0) := '0' & X"2";
    constant c_c64base_h  : unsigned(4 downto 0) := '0' & X"3";
    constant c_reubase_l  : unsigned(4 downto 0) := '0' & X"4";
    constant c_reubase_m  : unsigned(4 downto 0) := '0' & X"5";
    constant c_reubase_h  : unsigned(4 downto 0) := '0' & X"6";
    constant c_translen_l : unsigned(4 downto 0) := '0' & X"7";
    constant c_translen_h : unsigned(4 downto 0) := '0' & X"8";
    constant c_irqmask    : unsigned(4 downto 0) := '0' & X"9";
    constant c_control    : unsigned(4 downto 0) := '0' & X"A";
    -- extended registers
    constant c_size_read  : unsigned(4 downto 0) := '0' & X"C";
    constant c_start_delay: unsigned(4 downto 0) := '0' & X"D";
    constant c_rate_div   : unsigned(4 downto 0) := '0' & X"E";
    constant c_translen_x : unsigned(4 downto 0) := '0' & X"F";
    
    constant c_mode_toreu   : std_logic_vector(1 downto 0) := "00";
    constant c_mode_toc64   : std_logic_vector(1 downto 0) := "01";
    constant c_mode_swap    : std_logic_vector(1 downto 0) := "10";
    constant c_mode_verify  : std_logic_vector(1 downto 0) := "11";

end;
