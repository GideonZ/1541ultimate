library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package slot_bus_pkg is

    type t_slot_req is record
        read_address    : unsigned(8 downto 0); -- for async reads
        event_address   : unsigned(8 downto 0);
        read            : std_logic;
        write           : std_logic;
        data            : std_logic_vector(7 downto 0);
    end record;
    
    type t_slot_resp is record
        data        : std_logic_vector(7 downto 0);
        reg_output  : std_logic;
    end record;

    constant c_slot_req_init : t_slot_req := (
        data          => X"00",
        read          => '0',
        write         => '0',
        read_address  => "000000000",
        event_address => "000000000" );
     
    constant c_slot_resp_init : t_slot_resp := (
        data       => X"00",
        reg_output => '0' );
        
    type t_slot_req_array is array(natural range <>) of t_slot_req;
    type t_slot_resp_array is array(natural range <>) of t_slot_resp;
    
    function or_reduce(ar: t_slot_resp_array) return t_slot_resp;
end package;

package body slot_bus_pkg is
    function or_reduce(ar: t_slot_resp_array) return t_slot_resp is
        variable ret : t_slot_resp;
    begin
        ret := c_slot_resp_init;
        for i in ar'range loop
            ret.reg_output := ret.reg_output or ar(i).reg_output;
            ret.data := ret.data or ar(i).data;
        end loop;
        return ret;        
    end function or_reduce;
end package body;
    