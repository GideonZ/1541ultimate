-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : gray_code_pkg
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: gray code package, only the functions needed for the
--              asynchronous fifo
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package gray_code_pkg is

    ---------------------------------------------------------------------------
    -- type
    ---------------------------------------------------------------------------
    type t_gray is array (natural range <>) of std_logic;

    ---------------------------------------------------------------------------
    -- conversion functions
    ---------------------------------------------------------------------------
    function to_unsigned (arg : t_gray) return unsigned;
    function to_gray (arg : unsigned) return t_gray;

end gray_code_pkg;

-------------------------------------------------------------------------------
-- package body
-------------------------------------------------------------------------------

package body gray_code_pkg is

    function to_unsigned (arg : t_gray) return unsigned is
        alias myarg : t_gray(1 to arg'length) is arg; -- force direction 
        variable mybin : unsigned(myarg'range);
        variable result: unsigned(arg'range);
    begin
        for i in myarg'range loop
            if i = 1 then
                mybin(i) := myarg(i);
            else
                mybin(i) := myarg(i) xor mybin(i-1);
            end if;
        end loop;
        result := mybin;
        return result;
    end function;

    function to_gray (arg : unsigned) return t_gray is
        alias myarg : unsigned(1 to arg'length) is arg; -- force direction
        variable mygray : t_gray(myarg'range);
        variable result : t_gray(arg'range);
    begin
        for i in myarg'range loop
            if i = 1 then
                mygray(i) := myarg(i);
            else
                mygray(i) := myarg(i) xor myarg(i-1);
            end if;
        end loop;
        result := mygray;
        return result;
    end function;

end;
