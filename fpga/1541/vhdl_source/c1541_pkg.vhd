library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package c1541_pkg is

    constant c_drvreg_power     : unsigned(3 downto 0) := X"0";
    constant c_drvreg_reset     : unsigned(3 downto 0) := X"1";
    constant c_drvreg_address   : unsigned(3 downto 0) := X"2";
    constant c_drvreg_sensor    : unsigned(3 downto 0) := X"3";
    constant c_drvreg_inserted  : unsigned(3 downto 0) := X"4";
    constant c_drvreg_rammap    : unsigned(3 downto 0) := X"5";
    constant c_drvreg_side      : unsigned(3 downto 0) := X"6";
    constant c_drvreg_man_write : unsigned(3 downto 0) := X"7";
    constant c_drvreg_track     : unsigned(3 downto 0) := X"8";
    constant c_drvreg_status    : unsigned(3 downto 0) := X"9";
    constant c_drvreg_memmap    : unsigned(3 downto 0) := X"A";
    constant c_drvreg_audiomap  : unsigned(3 downto 0) := X"B";
    constant c_drvreg_diskchng  : unsigned(3 downto 0) := X"C";
    constant c_drvreg_drivetype : unsigned(3 downto 0) := X"D";
    constant c_drvreg_sound     : unsigned(3 downto 0) := X"E";

    constant c_drv_dirty_base   : unsigned(15 downto 0) := X"0800";
    constant c_drv_param_base   : unsigned(15 downto 0) := X"1000";

end;
