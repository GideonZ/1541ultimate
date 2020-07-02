
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
package reu_pkg is

    constant c_status     : unsigned(7 downto 0) := X"00";
    constant c_command    : unsigned(7 downto 0) := X"01";
    constant c_c64base_l  : unsigned(7 downto 0) := X"02";
    constant c_c64base_h  : unsigned(7 downto 0) := X"03";
    constant c_reubase_l  : unsigned(7 downto 0) := X"04";
    constant c_reubase_m  : unsigned(7 downto 0) := X"05";
    constant c_reubase_h  : unsigned(7 downto 0) := X"06";
    constant c_translen_l : unsigned(7 downto 0) := X"07";
    constant c_translen_h : unsigned(7 downto 0) := X"08";
    constant c_irqmask    : unsigned(7 downto 0) := X"09";
    constant c_control    : unsigned(7 downto 0) := X"0A";
    -- extended registers
    constant c_size_read  : unsigned(7 downto 0) := X"0C";
    constant c_start_delay: unsigned(7 downto 0) := X"0D";
    constant c_rate_div   : unsigned(7 downto 0) := X"0E";
    constant c_translen_x : unsigned(7 downto 0) := X"0F";
    
    constant c_mode_toreu   : std_logic_vector(1 downto 0) := "00";
    constant c_mode_toc64   : std_logic_vector(1 downto 0) := "01";
    constant c_mode_swap    : std_logic_vector(1 downto 0) := "10";
    constant c_mode_verify  : std_logic_vector(1 downto 0) := "11";

end;
