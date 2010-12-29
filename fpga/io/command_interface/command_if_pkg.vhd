library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
package command_if_pkg is

    -- io range registers
    constant c_slot_base        : unsigned(3 downto 0) := X"0"; 
    constant c_slot_enable      : unsigned(3 downto 0) := X"1";
    constant c_handshake_out    : unsigned(3 downto 0) := X"2";
    constant c_handshake_in     : unsigned(3 downto 0) := X"3";
    
end package;
