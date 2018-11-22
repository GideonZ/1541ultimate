--------------------------------------------------------------------------------
-- Entity: audio_type_pkg
-- Date:2018-08-02  
-- Author: gideon     
--
-- Description: Definition of audio type
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;


package audio_type_pkg is
    subtype t_audio     is signed(17 downto 0);
    type t_audio_array is array(natural range <>) of t_audio;
end package;
