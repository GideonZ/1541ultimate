--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: endianness_pkg
-- Date:2015-01-15  
-- Author: Gideon     
-- Description: 
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

package endianness_pkg is
    function byte_swap(a : std_logic_vector(31 downto 0)) return std_logic_vector;
end package;

package body endianness_pkg is
    function byte_swap(a : std_logic_vector(31 downto 0)) return std_logic_vector is
    begin
        return a(7 downto 0) & a(15 downto 8) & a(23 downto 16) & a(31 downto 24);
    end function;

end package body;

