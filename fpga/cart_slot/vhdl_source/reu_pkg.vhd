
library ieee;
    use ieee.std_logic_1164.all;

package reu_pkg is

    constant c_status     : std_logic_vector(4 downto 0) := '0' & X"0";
    constant c_command    : std_logic_vector(4 downto 0) := '0' & X"1";
    constant c_c64base_l  : std_logic_vector(4 downto 0) := '0' & X"2";
    constant c_c64base_h  : std_logic_vector(4 downto 0) := '0' & X"3";
    constant c_reubase_l  : std_logic_vector(4 downto 0) := '0' & X"4";
    constant c_reubase_m  : std_logic_vector(4 downto 0) := '0' & X"5";
    constant c_reubase_h  : std_logic_vector(4 downto 0) := '0' & X"6";
    constant c_translen_l : std_logic_vector(4 downto 0) := '0' & X"7";
    constant c_translen_h : std_logic_vector(4 downto 0) := '0' & X"8";
    constant c_irqmask    : std_logic_vector(4 downto 0) := '0' & X"9";
    constant c_control    : std_logic_vector(4 downto 0) := '0' & X"A";
    -- extended registers
    constant c_size_read  : std_logic_vector(4 downto 0) := '0' & X"C";
    constant c_start_delay: std_logic_vector(4 downto 0) := '0' & X"D";
    constant c_rate_div   : std_logic_vector(4 downto 0) := '0' & X"E";
    constant c_translen_x : std_logic_vector(4 downto 0) := '0' & X"F";
    
    constant c_mode_toreu   : std_logic_vector(1 downto 0) := "00";
    constant c_mode_toc64   : std_logic_vector(1 downto 0) := "01";
    constant c_mode_swap    : std_logic_vector(1 downto 0) := "10";
    constant c_mode_verify  : std_logic_vector(1 downto 0) := "11";

end;
