library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package icap_pkg is

    constant c_icap_fpga_type    : unsigned(3 downto 0) := X"0";
    constant c_icap_write        : unsigned(3 downto 0) := X"4";
    constant c_icap_pulse        : unsigned(3 downto 0) := X"8";
	
end package;
