library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package slot_bus_pkg is

    type t_slot_req is record
        bus_address     : unsigned(15 downto 0); -- for async reads and direct bus writes
        bus_write       : std_logic;
        io_address      : unsigned(15 downto 0); -- for late reads/writes
        io_read         : std_logic;
        io_read_early   : std_logic;
        io_write        : std_logic;
        late_write      : std_logic;
        data            : std_logic_vector(7 downto 0);
    end record;
    
    type t_slot_resp is record
        data        : std_logic_vector(7 downto 0);
        reg_output  : std_logic;
        irq         : std_logic;
    end record;

    constant c_slot_req_init : t_slot_req := (
        bus_address   => X"0000",
        bus_write     => '0',
        io_read_early => '0',
        io_address    => X"0000",
        io_read       => '0',
        io_write      => '0',
        late_write    => '0',
        data          => X"00" );
     
    constant c_slot_resp_init : t_slot_resp := (
        data       => X"00",
        reg_output => '0',
        irq        => '0' );
        
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
            if ar(i).reg_output='1' then
                ret.data := ret.data or ar(i).data;
            end if;
            ret.irq := ret.irq or ar(i).irq;
        end loop;
        return ret;        
    end function or_reduce;
end package body;
    