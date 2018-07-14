--------------------------------------------------------------------------------
-- Entity: nano_addresses_pkg
-- Date:2018-07-14  
-- Author: gideon     
--
-- Description: Package with addresses in Nano RAM
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package nano_addresses_pkg is

    constant Command          : unsigned(19 downto 0) := X"007E0";
    constant Command_DevEP    : unsigned(19 downto 0) := X"007E2";
    constant Command_Length   : unsigned(19 downto 0) := X"007E4";
    constant Command_MaxTrans : unsigned(19 downto 0) := X"007E6";
    constant Command_MemHi    : unsigned(19 downto 0) := X"007E8";
    constant Command_MemLo    : unsigned(19 downto 0) := X"007EA";
    constant Command_SplitCtl : unsigned(19 downto 0) := X"007EC";
    constant Command_Result   : unsigned(19 downto 0) := X"007EE";

    constant c_nano_simulation : unsigned(19 downto 0) := X"007D6";
    constant c_nano_dosuspend  : unsigned(19 downto 0) := X"007D8";
    constant c_nano_doreset    : unsigned(19 downto 0) := X"007DA";
    constant c_nano_busspeed   : unsigned(19 downto 0) := X"007DC";
    constant c_nano_enable     : unsigned(19 downto 0) := X"00800";

end package;
