--------------------------------------------------------------------------------
-- Entity: acia6551
-- Date:2018-11-13  
-- Author: gideon     
--
-- Description: Definitions of 6551.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package acia6551_pkg is
    constant c_addr_data_register       : unsigned(1 downto 0) := "00";
    constant c_addr_status_register     : unsigned(1 downto 0) := "01"; -- writing causes reset
    constant c_addr_command_register    : unsigned(1 downto 0) := "10";
    constant c_addr_control_register    : unsigned(1 downto 0) := "11";

    constant c_reg_rx_head  : unsigned(3 downto 0) := X"0";
    constant c_reg_rx_tail  : unsigned(3 downto 0) := X"1";
    constant c_reg_tx_head  : unsigned(3 downto 0) := X"2";
    constant c_reg_tx_tail  : unsigned(3 downto 0) := X"3";
    constant c_reg_control  : unsigned(3 downto 0) := X"4";
    constant c_reg_command  : unsigned(3 downto 0) := X"5";
    constant c_reg_enable   : unsigned(3 downto 0) := X"6";
    constant c_reg_handsh   : unsigned(3 downto 0) := X"7";
end package;
