library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package io_bus_pkg is

    type t_io_req is record
        read    : std_logic;
        write   : std_logic;
        address : unsigned(19 downto 0);
        data    : std_logic_vector(7 downto 0);
    end record;
    
    type t_io_resp is record
        data    : std_logic_vector(7 downto 0);
        ack     : std_logic;
    end record;

    constant c_io_req_init : t_io_req := (
        read    => '0',
        write   => '0',
        address => X"00000",
        data    => X"00" );
     
    constant c_io_resp_init : t_io_resp := (
        data    => X"00",
        ack     => '0' );
        
    type t_io_req_array is array(natural range <>) of t_io_req;
    type t_io_resp_array is array(natural range <>) of t_io_resp;
    
    function or_reduce(ar: t_io_resp_array) return t_io_resp;
end package;

package body io_bus_pkg is
    function or_reduce(ar: t_io_resp_array) return t_io_resp is
        variable ret : t_io_resp;
    begin
        ret := c_io_resp_init;
        for i in ar'range loop
            ret.ack := ret.ack or ar(i).ack;
            ret.data := ret.data or ar(i).data;
        end loop;
        return ret;        
    end function or_reduce;
end package body;
    