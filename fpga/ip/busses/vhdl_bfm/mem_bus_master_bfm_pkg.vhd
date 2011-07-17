
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library std;
use std.textio.all;

library work;
use work.mem_bus_pkg.all;

package mem_bus_master_bfm_pkg is

    type t_mem_bus_master_bfm_object;
    type p_mem_bus_master_bfm_object is access t_mem_bus_master_bfm_object;
    
    type t_mem_bus_bfm_command is ( e_mem_none, e_mem_read, e_mem_write );
    
    type t_mem_bus_master_bfm_object is record
        next_bfm    : p_mem_bus_master_bfm_object;
        name        : string(1 to 256);

        command     : t_mem_bus_bfm_command;
        poll_time   : time;
        tag         : std_logic_vector(7 downto 0);
        address     : unsigned(25 downto 0);
        data        : std_logic_vector(7 downto 0);

    end record;
    

    ------------------------------------------------------------------------------------
    shared variable mem_bus_master_bfms : p_mem_bus_master_bfm_object := null;
    ------------------------------------------------------------------------------------
    procedure register_mem_bus_master_bfm(named : string; variable pntr: inout p_mem_bus_master_bfm_object);
    procedure bind_mem_bus_master_bfm(named : string; variable pntr: inout p_mem_bus_master_bfm_object);
    ------------------------------------------------------------------------------------
    procedure mem_read(variable m : inout p_mem_bus_master_bfm_object; addr : unsigned; data : out std_logic_vector(7 downto 0));
    procedure mem_write(variable m : inout p_mem_bus_master_bfm_object; addr : unsigned; data : std_logic_vector(7 downto 0));

end mem_bus_master_bfm_pkg;


package body mem_bus_master_bfm_pkg is        

    procedure register_mem_bus_master_bfm(named         : string;
                                   variable pntr : inout p_mem_bus_master_bfm_object) is
    begin
        -- Allocate a new BFM object in memory
        pntr := new t_mem_bus_master_bfm_object;

        -- Initialize object
        pntr.next_bfm  := null;
        pntr.name(named'range) := named;

        -- add this pointer to the head of the linked list
        if mem_bus_master_bfms = null then -- first entry
            mem_bus_master_bfms := pntr;
        else -- insert new entry
            pntr.next_bfm := mem_bus_master_bfms;
            mem_bus_master_bfms := pntr;
        end if;
        pntr.tag := X"01";
        pntr.poll_time := 2 ns;
        
    end register_mem_bus_master_bfm;

    procedure bind_mem_bus_master_bfm(named         : string;
                               variable pntr : inout p_mem_bus_master_bfm_object) is
        variable p : p_mem_bus_master_bfm_object;
    begin
        pntr := null;
        wait for 1 ns;  -- needed to make sure that binding takes place after registration

        p := mem_bus_master_bfms; -- start at the root
        L1: while p /= null loop
            if p.name(named'range) = named then
                pntr := p;
                exit L1;
            else
                p := p.next_bfm;
            end if;
        end loop;
    end bind_mem_bus_master_bfm;

------------------------------------------------------------------------------

    procedure mem_read(variable m : inout p_mem_bus_master_bfm_object; addr : unsigned;
                      data : out std_logic_vector(7 downto 0)) is
        variable a_i : unsigned(25 downto 0);
    begin
        a_i := (others => '0');
        a_i(addr'length-1 downto 0) := addr;
        m.address := a_i;
        m.command := e_mem_read;
        while m.command /= e_mem_none loop
            wait for m.poll_time;
        end loop;
        data := m.data;
    end procedure;
                      
    procedure mem_write(variable m : inout p_mem_bus_master_bfm_object; addr : unsigned;
                       data : std_logic_vector(7 downto 0)) is
        variable a_i : unsigned(25 downto 0);
    begin
        a_i := (others => '0');
        a_i(addr'length-1 downto 0) := addr;
        m.address := a_i;
        m.command := e_mem_write;
        m.data    := data;
        while m.command /= e_mem_none loop
            wait for m.poll_time;
        end loop;
    end procedure;
end;

------------------------------------------------------------------------------
