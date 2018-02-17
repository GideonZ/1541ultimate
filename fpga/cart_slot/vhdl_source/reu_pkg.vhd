
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
package reu_pkg is

    constant c_status     : unsigned(4 downto 0) := "00000";
    constant c_command    : unsigned(4 downto 0) := "00001";
    constant c_c64base_l  : unsigned(4 downto 0) := "00010";
    constant c_c64base_h  : unsigned(4 downto 0) := "00011";
    constant c_reubase_l  : unsigned(4 downto 0) := "00100";
    constant c_reubase_m  : unsigned(4 downto 0) := "00101";
    constant c_reubase_h  : unsigned(4 downto 0) := "00110";
    constant c_translen_l : unsigned(4 downto 0) := "00111";
    constant c_translen_h : unsigned(4 downto 0) := "01000";
    constant c_irqmask    : unsigned(4 downto 0) := "01001";
    constant c_control    : unsigned(4 downto 0) := "01010";
    -- extended registers
    constant c_size_read  : unsigned(4 downto 0) := "01100";
    constant c_start_delay: unsigned(4 downto 0) := "01101";
    constant c_rate_div   : unsigned(4 downto 0) := "01110";
    constant c_translen_x : unsigned(4 downto 0) := "01111";
    
    constant c_mode_toreu   : std_logic_vector(1 downto 0) := "00";
    constant c_mode_toc64   : std_logic_vector(1 downto 0) := "01";
    constant c_mode_swap    : std_logic_vector(1 downto 0) := "10";
    constant c_mode_verify  : std_logic_vector(1 downto 0) := "11";

end;
