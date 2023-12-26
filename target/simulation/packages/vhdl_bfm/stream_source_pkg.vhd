--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: Bus functional model of a stream source
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

package stream_source_pkg is

    type t_stream_entry;
    type p_stream_entry is access t_stream_entry;
    type p_std_logic_vector is access std_logic_vector;

    type t_stream_entry is record
        data    : p_std_logic_vector;
        user    : p_std_logic_vector;
        last    : std_logic;
        nextp   : p_stream_entry;
    end record;

    type t_stream_source is record
        name    : string(1 to 80);
        head    : p_stream_entry;
        tail    : p_stream_entry;
    end record;

    type p_stream_source is access t_stream_source;    
    type t_stream_source_array is array(natural range <>) of p_stream_source;
    subtype h_stream_source is natural range 0 to 100;

    ---------------------------------------------------------------------------
    shared variable stream_sources : t_stream_source_array(1 to 100) := (others => null);
    ---------------------------------------------------------------------------
    procedure register_stream_source(
        named           :     string;
        variable handle : out h_stream_source);
    procedure bind_stream_source (
        named           :     string;
        variable handle : out h_stream_source);
    ---------------------------------------------------------------------------
    procedure put(handle : h_stream_source; data, user : std_logic_vector := ""; last : std_logic := '0');
    procedure get(handle : h_stream_source; data, user : out std_logic_vector; last : out std_logic);
    ---------------------------------------------------------------------------
end package;

package body stream_source_pkg is
    -- Memory model module registration into array
    procedure register_stream_source(
        named           :     string;
        variable handle : out h_stream_source) is
    begin
        handle := 0;
        L1 : for i in stream_sources'range loop
            if stream_sources(i) = null then
                --      report "my name is "& named;
                handle                              := i;
                stream_sources(i)                   := new t_stream_source;
                stream_sources(i).name(named'range) := named;
                stream_sources(i).head              := null;
                stream_sources(i).tail              := null;
                exit L1;
            end if;
        end loop;
    end procedure;

    -- Memory model module binding
    procedure bind_stream_source (
        named           :     string;
        variable handle : out h_stream_source) is
    begin
        handle := 0;
        wait for 1 ns;
        L1 : for i in stream_sources'range loop
            if stream_sources(i) /= null then
                if stream_sources(i).name(named'range) = named then
                    handle := i;
                    return;
                end if;
            end if;
        end loop;
        report "Can't find stream_source '"&named&"'."
            severity failure;
    end procedure;

    procedure put(handle : h_stream_source; data, user : std_logic_vector; last : std_logic) is
        variable src : p_stream_source;
        variable entry : p_stream_entry;
    begin
        src := stream_sources(handle);
        entry := new t_stream_entry;
        entry.data := new std_logic_vector(data'range);
        entry.data.all := data;
        entry.user := new std_logic_vector(user'range);
        entry.user.all := user;
        entry.last := last;
        entry.nextp := null;

        if src.head = null then -- empty list, link first entry
            src.head := entry;
            src.tail := entry;
        else -- entries already exist; add to tail
            src.tail.nextp := entry;
            src.tail := entry;
        end if;
    end procedure;

    procedure get(handle : h_stream_source; data, user : out std_logic_vector; last : out std_logic) is
        variable src : p_stream_source;
        variable entry : p_stream_entry;
    begin
        src := stream_sources(handle);
        entry := src.head;
        if entry /= null then
            data := entry.data.all;
            user := entry.user.all;
            last := entry.last;
            src.head := entry.nextp;
            deallocate(entry);
        end if;
    end procedure;

end;
