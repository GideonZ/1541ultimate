library ieee;
use ieee.std_logic_1164.all;

package track_pkg is

    type t_byte_array is array (natural range <>) of std_logic_vector(7 downto 0);

    constant sdrom_array : t_byte_array := (
        X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", 
        X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", 
        X"55", X"55", X"55", X"55", X"55", X"55", X"5F", X"FF", X"FF", X"F5", X"25", X"4B", X"52", X"94", X"B5", X"29", 
        X"4A", X"52", X"94", X"A4", X"A4", X"A4", X"A4", X"A4", X"A4", X"A4", X"A4", X"AB", X"BB", X"FF", X"FF", X"FF", 
        X"FF", X"FF", X"FD", X"57", X"56", X"A9", X"24", X"00", X"02", X"AB", X"53", X"BB", X"BB", X"BB", X"BB", X"BB", 
        X"BB", X"BB", X"BA", X"5A", X"96", X"A5", X"A9", X"6A", X"5A", X"96", X"A5", X"A9", X"6A", X"5A", X"96", X"A5", 
        X"A9", X"6A", X"5A", X"96", X"A5", X"A9", X"6A", X"5A", X"96", X"A5", X"A9", X"6A", X"5A", X"96", X"A5", X"A9" ); 

end;
