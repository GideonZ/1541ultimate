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

    constant Command          : unsigned(19 downto 0) := X"00600";
    constant Command_DevEP    : unsigned(19 downto 0) := X"00602";
    constant Command_Length   : unsigned(19 downto 0) := X"00604";
    constant Command_MaxTrans : unsigned(19 downto 0) := X"00606";
    constant Command_Interval : unsigned(19 downto 0) := X"00608";
    constant Command_LastFrame: unsigned(19 downto 0) := X"0060A";
    constant Command_SplitCtl : unsigned(19 downto 0) := X"0060C";
    constant Command_Result   : unsigned(19 downto 0) := X"0060E";
    constant Command_MemLo    : unsigned(19 downto 0) := X"00610";
    constant Command_MemHi    : unsigned(19 downto 0) := X"00612";
    constant Command_Started  : unsigned(19 downto 0) := X"00614";
    constant Command_Timeout  : unsigned(19 downto 0) := X"00616";
    constant Command_Size     : natural := 24; -- bytes    

    constant c_nano_simulation : unsigned(19 downto 0) := X"007D6";
    constant c_nano_dosuspend  : unsigned(19 downto 0) := X"007D8";
    constant c_nano_doreset    : unsigned(19 downto 0) := X"007DA";
    constant c_nano_busspeed   : unsigned(19 downto 0) := X"007DC";
    constant c_nano_enable     : unsigned(19 downto 0) := X"00800";

end package;
