
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library std;
use std.textio.all;

library work;
use work.io_bus_pkg.all;

package io_bus_bfm_pkg is

    type t_io_bus_bfm_object;
    type p_io_bus_bfm_object is access t_io_bus_bfm_object;
    
    type t_io_bfm_command is ( e_io_none, e_io_read, e_io_write );
    
    type t_io_bus_bfm_object is record
        next_bfm    : p_io_bus_bfm_object;
        name        : string(1 to 256);

        command     : t_io_bfm_command;
        address     : unsigned(19 downto 0);
        data        : std_logic_vector(7 downto 0);

    end record;
    

    ------------------------------------------------------------------------------------
    shared variable io_bus_bfms : p_io_bus_bfm_object := null;
    ------------------------------------------------------------------------------------
    procedure register_io_bus_bfm(named : string; variable pntr: inout p_io_bus_bfm_object);
    procedure bind_io_bus_bfm(named : string; variable pntr: inout p_io_bus_bfm_object);
    ------------------------------------------------------------------------------------
    procedure io_read(variable io : inout p_io_bus_bfm_object; addr : unsigned; data : out std_logic_vector(7 downto 0));
    procedure io_write(variable io : inout p_io_bus_bfm_object; addr : unsigned; data : std_logic_vector(7 downto 0));

end io_bus_bfm_pkg;


package body io_bus_bfm_pkg is        

    procedure register_io_bus_bfm(named         : string;
                                   variable pntr : inout p_io_bus_bfm_object) is
    begin
        -- Allocate a new BFM object in memory
        pntr := new t_io_bus_bfm_object;

        -- Initialize object
        pntr.next_bfm  := null;
        pntr.name(named'range) := named;

        -- add this pointer to the head of the linked list
        if io_bus_bfms = null then -- first entry
            io_bus_bfms := pntr;
        else -- insert new entry
            pntr.next_bfm := io_bus_bfms;
            io_bus_bfms := pntr;
        end if;
    end register_io_bus_bfm;

    procedure bind_io_bus_bfm(named         : string;
                               variable pntr : inout p_io_bus_bfm_object) is
        variable p : p_io_bus_bfm_object;
    begin
        pntr := null;
        wait for 1 ns;  -- needed to make sure that binding takes place after registration

        p := io_bus_bfms; -- start at the root
        L1: while p /= null loop
            if p.name(named'range) = named then
                pntr := p;
                exit L1;
            else
                p := p.next_bfm;
            end if;
        end loop;
    end bind_io_bus_bfm;

------------------------------------------------------------------------------

    procedure io_read(variable io : inout p_io_bus_bfm_object; addr : unsigned;
                      data : out std_logic_vector(7 downto 0)) is
        variable a_i : unsigned(19 downto 0);
    begin
        a_i := (others => '0');
        a_i(addr'length-1 downto 0) := addr;
        io.address := a_i;
        io.command := e_io_read;
        while io.command /= e_io_none loop
            wait for 10 ns;
        end loop;
        data := io.data;
    end procedure;
                      
    procedure io_write(variable io : inout p_io_bus_bfm_object; addr : unsigned;
                       data : std_logic_vector(7 downto 0)) is
        variable a_i : unsigned(19 downto 0);
    begin
        a_i := (others => '0');
        a_i(addr'length-1 downto 0) := addr;
        io.address := a_i;
        io.command := e_io_write;
        io.data    := data;
        while io.command /= e_io_none loop
            wait for 10 ns;
        end loop;
    end procedure;
end;

------------------------------------------------------------------------------
