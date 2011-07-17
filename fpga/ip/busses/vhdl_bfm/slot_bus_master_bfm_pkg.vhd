
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library std;
use std.textio.all;

library work;
use work.slot_bus_pkg.all;

package slot_bus_master_bfm_pkg is

    type t_slot_bus_master_bfm_object;
    type p_slot_bus_master_bfm_object is access t_slot_bus_master_bfm_object;
    
    type t_slot_bus_bfm_command is ( e_slot_none, e_slot_io_read, e_slot_bus_read,
    											  e_slot_io_write, e_slot_bus_write );
    
    type t_slot_bus_master_bfm_object is record
        next_bfm    : p_slot_bus_master_bfm_object;
        name        : string(1 to 256);

        command     : t_slot_bus_bfm_command;
        poll_time   : time;
        address     : unsigned(15 downto 0);
        data        : std_logic_vector(7 downto 0);
		irq_pending : boolean;
    end record;
    

    ------------------------------------------------------------------------------------
    shared variable slot_bus_master_bfms : p_slot_bus_master_bfm_object := null;
    ------------------------------------------------------------------------------------
    procedure register_slot_bus_master_bfm(named : string; variable pntr: inout p_slot_bus_master_bfm_object);
    procedure bind_slot_bus_master_bfm(named : string; variable pntr: inout p_slot_bus_master_bfm_object);
    ------------------------------------------------------------------------------------
    procedure slot_io_read(variable m : inout p_slot_bus_master_bfm_object; addr : unsigned; data : out std_logic_vector(7 downto 0));
    procedure slot_io_write(variable m : inout p_slot_bus_master_bfm_object; addr : unsigned; data : std_logic_vector(7 downto 0));
    procedure slot_bus_read(variable m : inout p_slot_bus_master_bfm_object; addr : unsigned; data : out std_logic_vector(7 downto 0));
    procedure slot_bus_write(variable m : inout p_slot_bus_master_bfm_object; addr : unsigned; data : std_logic_vector(7 downto 0));
	procedure slot_wait_irq(variable m : inout p_slot_bus_master_bfm_object);
		
end slot_bus_master_bfm_pkg;


package body slot_bus_master_bfm_pkg is        

    procedure register_slot_bus_master_bfm(named         : string;
                                   variable pntr : inout p_slot_bus_master_bfm_object) is
    begin
        -- Allocate a new BFM object in memory
        pntr := new t_slot_bus_master_bfm_object;

        -- Initialize object
        pntr.next_bfm  := null;
        pntr.name(named'range) := named;

        -- add this pointer to the head of the linked list
        if slot_bus_master_bfms = null then -- first entry
            slot_bus_master_bfms := pntr;
        else -- insert new entry
            pntr.next_bfm := slot_bus_master_bfms;
            slot_bus_master_bfms := pntr;
        end if;
        pntr.irq_pending := false;
        pntr.poll_time := 10 ns;
    end register_slot_bus_master_bfm;

    procedure bind_slot_bus_master_bfm(named         : string;
                               variable pntr : inout p_slot_bus_master_bfm_object) is
        variable p : p_slot_bus_master_bfm_object;
    begin
        pntr := null;
        wait for 1 ns;  -- needed to make sure that binding takes place after registration

        p := slot_bus_master_bfms; -- start at the root
        L1: while p /= null loop
            if p.name(named'range) = named then
                pntr := p;
                exit L1;
            else
                p := p.next_bfm;
            end if;
        end loop;
    end bind_slot_bus_master_bfm;

------------------------------------------------------------------------------

    procedure slot_bus_read(variable m : inout p_slot_bus_master_bfm_object; addr : unsigned;
                      data : out std_logic_vector(7 downto 0)) is
        variable a_i : unsigned(15 downto 0);
    begin
        a_i := (others => '0');
        a_i(addr'length-1 downto 0) := addr;
        m.address := a_i;
        m.command := e_slot_bus_read;
        while m.command /= e_slot_none loop
            wait for m.poll_time;
        end loop;
        data := m.data;
    end procedure;
                      
    procedure slot_io_read(variable m : inout p_slot_bus_master_bfm_object; addr : unsigned;
                           data : out std_logic_vector(7 downto 0)) is
        variable a_i : unsigned(15 downto 0);
    begin
        a_i := (others => '0');
        a_i(addr'length-1 downto 0) := addr;
        m.address := a_i;
        m.command := e_slot_io_read;
        while m.command /= e_slot_none loop
            wait for m.poll_time;
        end loop;
        data := m.data;
    end procedure;

    procedure slot_bus_write(variable m : inout p_slot_bus_master_bfm_object; addr : unsigned;
                       data : std_logic_vector(7 downto 0)) is
        variable a_i : unsigned(15 downto 0);
    begin
        a_i := (others => '0');
        a_i(addr'length-1 downto 0) := addr;
        m.address := a_i;
        m.command := e_slot_bus_write;
        m.data    := data;
        while m.command /= e_slot_none loop
            wait for m.poll_time;
        end loop;
    end procedure;

    procedure slot_io_write(variable m : inout p_slot_bus_master_bfm_object; addr : unsigned;
                       data : std_logic_vector(7 downto 0)) is
        variable a_i : unsigned(15 downto 0);
    begin
        a_i := (others => '0');
        a_i(addr'length-1 downto 0) := addr;
        m.address := a_i;
        m.command := e_slot_io_write;
        m.data    := data;
        while m.command /= e_slot_none loop
            wait for m.poll_time;
        end loop;
    end procedure;

	procedure slot_wait_irq(variable m : inout p_slot_bus_master_bfm_object) is
	begin
		while not m.irq_pending loop
            wait for m.poll_time;
        end loop;
	end procedure;
end;

------------------------------------------------------------------------------
