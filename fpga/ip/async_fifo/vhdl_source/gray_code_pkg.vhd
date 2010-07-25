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
    -- conversion function
    ---------------------------------------------------------------------------
    function to_unsigned (arg : t_gray) return unsigned;

    ---------------------------------------------------------------------------
    -- arithmetic function
    ---------------------------------------------------------------------------
    function increment (arg : t_gray) return t_gray;

end gray_code_pkg;

-------------------------------------------------------------------------------
-- package body
-------------------------------------------------------------------------------

package body gray_code_pkg is

    function to_unsigned (arg : t_gray) return unsigned is
        variable mybin : unsigned(arg'range);
        variable temp  : std_logic;
    begin
        for i in arg'low to arg'high-1 loop
            temp := '0';
            for j in i+1 to arg'high loop
                temp := temp xor arg(j);
            end loop;
            mybin(i) := arg(i) xor temp;
        end loop;
        mybin(mybin'high) := arg(arg'high);
        return mybin;
    end to_unsigned;

    function increment (arg : t_gray) return t_gray is
        alias xarg       : t_gray((arg'length-1) downto 0) is arg;
        variable grayinc : t_gray(xarg'range);
        variable temp    : std_logic;
    begin
        for i in xarg'range loop
            -- rule: high downto i: xnor    
            -- i-1: and
            -- i-2 downto 0: and not
            temp := '0';
            if i = xarg'high then
                temp := xarg(i) xor xarg(i-1);
            else
                for j in i to arg'high loop
                    temp := temp xor xarg(j);
                end loop;
                temp := not(temp);
                if i >= 1 then
                    temp := temp and xarg(i-1);
                end if;
            end if;

            if i >= 2 then
                for j in 0 to i-2 loop
                    temp := temp and not(xarg(j));
                end loop;
            end if;
            grayinc(i) := xarg(i) xor temp;
        end loop;
        return grayinc;
    end increment;

end;



