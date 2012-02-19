-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2004 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- Title      : Flat Memory Model package
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This package implements a memory model that can be used
--              as or in bus functional models. It implements different
--              banks, such that only one package is needed for all memories
--              in the whole project. These banks are dynamic, just like
--              the contents of the memories. Internally, this memory model
--              is 32-bit, but can be accessed by means of functions and
--              procedures that exist in various widths.
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library std;
use std.textio.all;

library work;
use work.tl_file_io_pkg.all;
use work.tl_string_util_pkg.all;

package tl_flat_memory_model_pkg is

    constant c_fm_max_bank    : integer := 255;
    constant c_fm_max_sector  : integer := 65535;
    constant c_fm_sector_size : integer := 16384;

    subtype t_byte is std_logic_vector(7 downto 0);

    type flat_mem_sector_t is array(0 to c_fm_sector_size-1) of integer;  -- each sector is 64kB
    type flat_mem_sector_p is access flat_mem_sector_t;
    type flat_mem_bank_t is array(0 to c_fm_max_sector) of flat_mem_sector_p;  -- there are 64k sectors (4 GB)
    type flat_mem_bank_p is access flat_mem_bank_t;

    -- we need to use a handle rather than a pointer, because we can't pass pointers in function calls
    -- Hence, we don't use a linked list, but an array.
    type flat_mem_object_t is record
        path : string(1 to 256);
        name : string(1 to 128);
        bank : flat_mem_bank_p;
    end record;

    type flat_mem_object_p is access flat_mem_object_t;

    type flat_mem_array_t is array(1 to c_fm_max_bank) of flat_mem_object_p;

    subtype h_mem_object is integer range 0 to c_fm_max_bank;

    ---------------------------------------------------------------------------
    shared variable flat_memories : flat_mem_array_t := (others => null);
    ---------------------------------------------------------------------------
    procedure register_mem_model(
        path            :     string;
        named           :     string;
        variable handle : out h_mem_object);
    procedure bind_mem_model (
        named           :     string;
        variable handle : out h_mem_object);
    ---------------------------------------------------------------------------

    -- Low level calls
    impure function read_memory(
        bank   : integer;
        sector : integer;
        entry  : integer)
        return integer;
    procedure write_memory(
        bank   : integer;
        sector : integer;
        entry  : integer;
        data   : integer);
    procedure clear_memory(
        bank : integer);
    procedure clean_up;

    -- 32-bit address/data access calls
    impure function read_memory_32(
        bank    : integer;
        address : std_logic_vector(31 downto 0))
        return std_logic_vector;
    procedure write_memory_32(
        bank    : integer;
        address : std_logic_vector(31 downto 0);
        data    : std_logic_vector(31 downto 0));
    procedure write_memory_be(
        bank    : integer;
        address : std_logic_vector(31 downto 0);
        data    : std_logic_vector(31 downto 0);
        be      : std_logic_vector(3 downto 0));

    -- 16-bit address/data access calls
    impure function read_memory_16(
        bank    : integer;
        address : std_logic_vector(31 downto 0))
        return std_logic_vector;
    procedure write_memory_16(
        bank    : integer;
        address : std_logic_vector(31 downto 0);
        data    : std_logic_vector(15 downto 0));

    -- 8-bit address/data access calls
    impure function read_memory_8(
        bank    : integer;
        address : std_logic_vector(31 downto 0))
        return std_logic_vector;
    procedure write_memory_8(
        bank    : integer;
        address : std_logic_vector(31 downto 0);
        data    : std_logic_vector(7 downto 0));

    -- integer direct access calls
    impure function read_memory_int(
        bank    : integer;
        address : integer )
        return integer;
    
    procedure write_memory_int(
        bank    : integer;
        address : integer;
        data    : integer );
    

    -- File Access Procedures
    procedure load_memory(
        filename : string;
        bank     : integer;
        address  : std_logic_vector(31 downto 0));

    procedure save_memory(
        filename : string;
        bank     : integer;
        address  : std_logic_vector(31 downto 0);
        length   : integer);

    procedure load_memory_hex(
        filename : string;
        bank     : integer);

    procedure save_memory_hex(
        filename : string;
        bank     : integer;
        address  : std_logic_vector(31 downto 0);
        length   : integer);

end package;

package body tl_flat_memory_model_pkg is
    -- Memory model module registration into array
    procedure register_mem_model(
        path            :     string;
        named           :     string;
        variable handle : out h_mem_object) is
    begin
        handle := 0;
        L1 : for i in flat_memories'range loop
            if flat_memories(i) = null then
                --      report "my name is "& named;
                handle                             := i;
                flat_memories(i)                   := new flat_mem_object_t;
                flat_memories(i).path(path'range)  := path;
                flat_memories(i).name(named'range) := named;
                flat_memories(i).bank              := new flat_mem_bank_t;
                exit L1;
            end if;
        end loop;
    end procedure register_mem_model;

    -- Memory model module binding
    procedure bind_mem_model (
        named           :     string;
        variable handle : out h_mem_object) is
    begin
        handle := 0;
        wait for 1 ns;
        L1 : for i in flat_memories'range loop
            if flat_memories(i) /= null then
                if flat_memories(i).name(named'range) = named or
                   flat_memories(i).path(named'range) = named then
                    handle := i;
                    return;
                end if;
            end if;
        end loop;
        report "Can't find memory model '"&named&"'."
            severity failure;
    end procedure bind_mem_model;

    -- Base calls
    impure function read_memory(
        bank   : integer;
        sector : integer;
        entry  : integer) return integer is
    begin
        if flat_memories(bank) = null then
            return 0;
        end if;
        if flat_memories(bank).bank(sector) = null then
            return 0;
        end if;
        return flat_memories(bank).bank(sector).all(entry);
    end function read_memory;

    procedure write_memory(
        bank   : integer;
        sector : integer;
        entry  : integer;
        data   : integer) is
    begin
        if flat_memories(bank) = null then
            flat_memories(bank)                          := new flat_mem_object_t;
            flat_memories(bank).bank(0 to c_fm_max_sector) := (others => null);
        end if;

        if flat_memories(bank).bank(sector) = null then
            flat_memories(bank).bank(sector)                          := new flat_mem_sector_t;
            flat_memories(bank).bank(sector).all(0 to c_fm_sector_size-1) := (others => 0);
        end if;

        flat_memories(bank).bank(sector).all(entry) := data;
    end procedure write_memory;

    procedure clear_memory(bank : integer) is
    begin
        if flat_memories(bank) /= null then
            for i in flat_memories(bank).bank'range loop
                if flat_memories(bank).bank(i) /= null then
                    deallocate(flat_memories(bank).bank(i));
                end if;
            end loop;
            deallocate(flat_memories(bank));
            flat_memories(bank) := null;
        end if;
    end procedure clear_memory;

    procedure clean_up is
    begin
        for i in flat_memories'range loop
            clear_memory(i);
        end loop;
    end procedure clean_up;

    -- 32-bit address/data access calls
    impure function read_memory_32(
        bank    : integer;
        address : std_logic_vector(31 downto 0))
        return std_logic_vector
    is
        variable sector_idx : integer;
        variable entry_idx  : integer;
    begin
        sector_idx := to_integer(unsigned(address(31 downto 16)));
        entry_idx  := to_integer(unsigned(address(15 downto 2)));
        return std_logic_vector(to_signed(read_memory(bank, sector_idx, entry_idx), 32));
    end function read_memory_32;

    procedure write_memory_32(
        bank    : integer;
        address : std_logic_vector(31 downto 0);
        data    : std_logic_vector(31 downto 0))
    is
        variable sector_idx : integer;
        variable entry_idx  : integer;
    begin
        sector_idx := to_integer(unsigned(address(31 downto 16)));
        entry_idx  := to_integer(unsigned(address(15 downto 2)));
        write_memory(bank, sector_idx, entry_idx, to_integer(signed(data)));
    end procedure write_memory_32;

    procedure write_memory_be(
        bank    : integer;
        address : std_logic_vector(31 downto 0);
        data    : std_logic_vector(31 downto 0);
        be      : std_logic_vector(3 downto 0))
    is
        variable sector_idx : integer;
        variable entry_idx  : integer;
        variable read_data  : std_logic_vector(31 downto 0);
        variable L          : line;
    begin
        --write_s(L, "Writing " & vec_to_hex(data, 8) & " to location " & vec_to_hex(address, 8));
        --writeline(output, L);
        
        sector_idx := to_integer(unsigned(address(31 downto 16)));
        entry_idx  := to_integer(unsigned(address(15 downto 2)));
        read_data  := std_logic_vector(to_signed(read_memory(bank, sector_idx, entry_idx), 32));
        for i in be'range loop
            if to_x01(be(i)) = '1' then
                read_data(7+8*i downto 8*i) := data(7+8*i downto 8*i);
            end if;
        end loop;
        write_memory(bank, sector_idx, entry_idx, to_integer(signed(read_data)));
    end procedure write_memory_be;

    -- 16-bit address/data access calls
    impure function read_memory_16(
        bank    : integer;
        address : std_logic_vector(31 downto 0))
        return std_logic_vector
    is
        variable sector_idx : integer;
        variable entry_idx  : integer;
        variable read_data  : std_logic_vector(31 downto 0);
    begin
        sector_idx := to_integer(unsigned(address(31 downto 16)));
        entry_idx  := to_integer(unsigned(address(15 downto 2)));
        read_data  := std_logic_vector(to_signed(read_memory(bank, sector_idx, entry_idx), 32));
        if address(1) = '0' then
            return read_data(15 downto 0);
        else
            return read_data(31 downto 16);
        end if;
    end function read_memory_16;

    procedure write_memory_16(
        bank    : integer;
        address : std_logic_vector(31 downto 0);
        data    : std_logic_vector(15 downto 0))
    is
        variable be_temp    : std_logic_vector(3 downto 0);
        variable write_data : std_logic_vector(31 downto 0);
    begin
        write_data := data & data;
        be_temp    := address(1) & address(1) & not address(1) & not address(1);
        write_memory_be(bank, address, write_data, be_temp);
    end procedure write_memory_16;

    -- 8-bit address/data access calls
    impure function read_memory_8(
        bank    : integer;
        address : std_logic_vector(31 downto 0))
        return std_logic_vector
    is
        variable sector_idx : integer;
        variable entry_idx  : integer;
        variable read_data  : std_logic_vector(31 downto 0);
    begin
        sector_idx := to_integer(unsigned(address(31 downto 16)));
        entry_idx  := to_integer(unsigned(address(15 downto 2)));
        read_data  := std_logic_vector(to_signed(read_memory(bank, sector_idx, entry_idx), 32));
        case address(1 downto 0) is
            when "11" =>
                return read_data(31 downto 24);
            when "01" =>
                return read_data(15 downto 8);
            when "10" =>
                return read_data(23 downto 16);
            when others =>
                return read_data(7 downto 0);
        end case;
    end function read_memory_8;

    procedure write_memory_8(
        bank    : integer;
        address : std_logic_vector(31 downto 0);
        data    : std_logic_vector(7 downto 0))
    is
        variable be_temp    : std_logic_vector(3 downto 0) := (others => '0');
        variable write_data : std_logic_vector(31 downto 0);
    begin
        write_data                                       := data & data & data & data;
        be_temp(to_integer(unsigned(address(1 downto 0)))) := '1';
        write_memory_be(bank, address, write_data, be_temp);
    end procedure write_memory_8;

    -- Integer direct procedures
    impure function read_memory_int(
        bank    : integer;
        address : integer )
        return integer is

        variable sect, index    : integer;
    begin
        sect  := address / c_fm_sector_size;
        index := address mod c_fm_sector_size;
        return read_memory(bank, sect, index);
    end function read_memory_int;
    
    procedure write_memory_int(
        bank    : integer;
        address : integer;
        data    : integer ) is

        variable sect, index    : integer;
    begin
        sect  := address / c_fm_sector_size;
        index := address mod c_fm_sector_size;
        write_memory(bank, sect, index, data);        
    end procedure write_memory_int;

    -- File access procedures

    -- not a public procedure.
    procedure read_binary_file(
        file myfile    :       t_binary_file;
        bank           :       integer;
        startaddr      :       std_logic_vector(31 downto 0);
        variable myrec : inout t_binary_file_rec)
    is
        variable addr       : unsigned(31 downto 0);
        variable data       : std_logic_vector(7 downto 0);
        variable i          : integer;
        variable sector_idx : integer;
        variable entry_idx  : integer;
    begin
        addr := unsigned(startaddr);

        if startaddr(1 downto 0) = "00" then
            sector_idx := to_integer(addr(31 downto 16));
            entry_idx  := to_integer(addr(15 downto 2));

            aligned : while true loop
                if EndFile(myfile) then
                    exit aligned;
                end if;

                read(myfile, i);
                write_memory(bank, sector_idx, entry_idx, i);

                if entry_idx = c_fm_sector_size-1 then
                    entry_idx := 0;
                    if sector_idx = c_fm_max_sector then
                        sector_idx := 0;
                    else
                        sector_idx := sector_idx + 1;
                    end if;
                else
                    entry_idx := entry_idx + 1;
                end if;
            end loop;
        else
            unaligned : while true loop
                if EndFile(myfile) and myrec.Offset = 0 then
                    exit unaligned;
                end if;

                read_byte(myfile, data, myrec);
                write_memory_8(bank, std_logic_vector(addr), data);
                addr := addr + 1;
            end loop;
        end if;
    end read_binary_file;

    -- not a public procedure
    procedure read_hex_file (
        file myfile : text;
        bank        : integer)
    is
        variable L        : line;
        variable addr     : unsigned(31 downto 0) := (others => '0');
        variable c        : character;
        variable data     : t_byte;
        variable sum      : unsigned(7 downto 0);
        variable rectype  : t_byte;
        variable tmp_addr : std_logic_vector(15 downto 0);
        variable fileend  : boolean;
        variable linenr   : integer                       := 0;
        variable len      : integer;
    begin
        
        outer : while true loop
            if EndFile(myfile) then
                report "Missing end of file record."
                    severity warning;
                return;
            end if;

            -- search for lines starting with ':'
            start : while true loop
                readline(myfile, L);
                linenr := linenr + 1;
                read(L, c);
                if c = ':' then
                    exit start;
                end if;
            end loop;

            -- parse the rest of the line
            sum := X"00";
            get_byte_from_file(myfile, L, fileend, data);
            len := to_integer(unsigned(data));
            get_byte_from_file(myfile, L, fileend, tmp_addr(15 downto 8));
            get_byte_from_file(myfile, L, fileend, tmp_addr(7 downto 0));
            get_byte_from_file(myfile, L, fileend, rectype);

            sum := sum - (unsigned(data) + unsigned(tmp_addr(15 downto 8)) + unsigned(tmp_addr(7 downto 0)) + unsigned(rectype));

            case rectype is
                when X"00" =>           -- data record
                    addr(15 downto 0) := unsigned(tmp_addr);
                    for i in 0 to len-1 loop
                        get_byte_from_file(myfile, L, fileend, data);
                        sum  := sum - unsigned(data);
                        write_memory_8(bank, std_logic_vector(addr), data);
                        addr := addr + 1;
                    end loop;
                    
                when X"01" =>           -- end of file record
                    return;
                    
                when X"04" =>           -- extended linear address record
                    get_byte_from_file(myfile, L, fileend, data);
                    addr(31 downto 24) := unsigned(data);
                    sum := sum - addr(31 downto 24);
                    get_byte_from_file(myfile, L, fileend, data);
                    addr(23 downto 16) := unsigned(data);
                    sum := sum - addr(23 downto 16);

                when others =>
                    report "Unexpected record type " & vec_to_hex(rectype, 2)
                        severity warning;
                    return;
                    
            end case;

            -- check checksum
            get_byte_from_file(myfile, L, fileend, data);
            assert sum = unsigned(data)
                report "Warning: Checksum incorrect at line: " & integer'image(linenr)
                severity warning;
        end loop;
    end read_hex_file;

    -- public procedure:
    procedure load_memory(
        filename : string;
        bank     : integer;
        address  : std_logic_vector(31 downto 0))
    is
        variable stat  : file_open_status;
        file myfile    : t_binary_file;
        variable myrec : t_binary_file_rec;
    begin
        -- open file
        file_open(stat, myfile, filename, read_mode);
        assert (stat = open_ok)
            report "Could not open file " & filename & " for reading."
            severity failure;
        init_record(myrec);
        read_binary_file (myfile, bank, address, myrec);
        file_close(myfile);
    end load_memory;

    -- public procedure:
    procedure load_memory_hex(
        filename : string;
        bank     : integer)
    is
        variable stat : file_open_status;
        file myfile   : text;
    begin
        -- open file
        file_open(stat, myfile, filename, read_mode);
        assert (stat = open_ok)
            report "Could not open file " & filename & " for reading."
            severity failure;
        read_hex_file (myfile, bank);
        file_close(myfile);
    end load_memory_hex;

    -- not a public procedure.
    procedure write_binary_file(
        file myfile    :       t_binary_file;
        bank           :       integer;
        startaddr      :       std_logic_vector(31 downto 0);
        length         :       integer;
        variable myrec : inout t_binary_file_rec)
    is
        variable addr       : unsigned(31 downto 0);
        variable data       : std_logic_vector(7 downto 0);
        variable i          : integer;
        variable sector_idx : integer;
        variable entry_idx  : integer;
        variable remaining  : integer;
    begin
        addr := unsigned(startaddr);

        if startaddr(1 downto 0) = "00" then
            sector_idx := to_integer(addr(31 downto 16));
            entry_idx  := to_integer(addr(15 downto 2));
            remaining  := (length + 3) / 4;

            aligned : while remaining > 0 loop
                
                i         := read_memory(bank, sector_idx, entry_idx);
                write(myfile, i);
                remaining := remaining - 1;

                if entry_idx = c_fm_sector_size-1 then
                    if sector_idx = c_fm_max_sector then
                        sector_idx := 0;
                    else
                        sector_idx := sector_idx + 1;
                    end if;
                else
                    entry_idx := entry_idx + 1;
                end if;
            end loop;
        else
            remaining := length;
            unaligned : while remaining > 0 loop
                data      := read_memory_8(bank, std_logic_vector(addr));
                write_byte(myfile, data, myrec);
                addr      := addr + 1;
                remaining := remaining - 1;
            end loop;
            purge(myfile, myrec);
        end if;
    end write_binary_file;

    -- not a public procedure.
    procedure write_hex_file(
        file myfile : text;
        bank        : integer;
        startaddr   : std_logic_vector(31 downto 0);
        length      : integer)
    is
        variable addr       : std_logic_vector(31 downto 0);
        variable data       : std_logic_vector(7 downto 0);
        variable sector_idx : integer;
        variable entry_idx  : integer;
        variable remaining  : integer;
        variable maxlen     : integer;
        variable sum        : unsigned(7 downto 0);
        variable L          : line;
        variable prev_hi    : std_logic_vector(31 downto 16) := (others => '-');
    begin
        addr      := startaddr;
        remaining := length;
        unaligned : while remaining > 0 loop
            -- check if we need to write a new extended address record
            if addr(31 downto 16) /= prev_hi then
                write_string(L, ":02000004");
                write(L, vec_to_hex(addr(31 downto 16), 4));
                write(L, vec_to_hex(std_logic_vector(X"FA" - unsigned(addr(31 downto 24)) - unsigned(addr(23 downto 16))), 2));
                writeline(myfile, L);
                prev_hi := addr(31 downto 16);
            end if;

            -- check for maximum length (until 64k boundary)
            maxlen := to_integer(X"10000" - unsigned(X"0" & addr(15 downto 0)));
            if maxlen > 16 then maxlen := 16; end if;

            -- create data record
            sum := X"00";
            write(L, ':');
            write(L, vec_to_hex(std_logic_vector(to_unsigned(maxlen, 8)), 2));
            write(L, vec_to_hex(addr(15 downto 0), 4));
            write_string(L, "00");
            sum := sum - maxlen;
            sum := sum - unsigned(addr(15 downto 8));
            sum := sum - unsigned(addr(7 downto 0));
            for i in 1 to maxlen loop
                data := read_memory_8(bank, addr);
                sum  := sum - unsigned(data);
                write(L, vec_to_hex(data, 2));
                addr := std_logic_vector(unsigned(addr) + 1);
            end loop;
            remaining := remaining - maxlen;
            write(L, vec_to_hex(std_logic_vector(sum), 2));
            writeline(myfile, L);
        end loop;
        write_string(L, ":00000001");
        writeline(myfile, L);
        
    end write_hex_file;

    -- public procedure:
    procedure save_memory(
        filename : string;
        bank     : integer;
        address  : std_logic_vector(31 downto 0);
        length   : integer)
    is
        variable stat  : file_open_status;
        file myfile    : t_binary_file;
        variable myrec : t_binary_file_rec;
    begin
        -- open file
        file_open(stat, myfile, filename, write_mode);
        assert (stat = open_ok)
            report "Could not open file " & filename & " for writing."
            severity failure;
        init_record(myrec);
        write_binary_file (myfile, bank, address, length, myrec);
        file_close(myfile);
    end save_memory;

    -- public procedure:
    procedure save_memory_hex(
        filename : string;
        bank     : integer;
        address  : std_logic_vector(31 downto 0);
        length   : integer)
    is
        variable stat : file_open_status;
        file myfile   : text;
    begin
        -- open file
        file_open(stat, myfile, filename, write_mode);
        assert (stat = open_ok)
            report "Could not open file " & filename & " for writing."
            severity failure;
        write_hex_file (myfile, bank, address, length);
        file_close(myfile);
    end save_memory_hex;
end;
