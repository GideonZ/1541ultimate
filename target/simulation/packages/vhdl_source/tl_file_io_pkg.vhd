-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006 TECHNOLUTION BV, GOUDA NL
-- | =======          I                   ==          I    =
-- |    I             I                    I          I
-- |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===
-- |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I
-- |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I
-- |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I
-- |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I
-- |                 +---------------------------------------------------+
-- +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|
--      |            |             ++++++++++++++++++++++++++++++++++++++|
--      +------------+                          +++++++++++++++++++++++++|
--                                                         ++++++++++++++|
--              A U T O M A T I O N     T E C H N O L O G Y         +++++|
--
-------------------------------------------------------------------------------
-- Title      : file io package
-- Author     : Gideon Zweijtzer  <Gideon.Zweijtzer@Technolution.NL>
-------------------------------------------------------------------------------
-- Description: File IO routines
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library std;
use std.textio.all;

library work;
use work.tl_string_util_pkg.all;

package tl_file_io_pkg is
    
    type t_slv8_array is array(natural range <>) of std_logic_vector(7 downto 0);
    
    procedure get_char_from_file(file my_file : text; my_line : inout line; file_end : out boolean; hex : out character);
    procedure get_byte_from_file(file my_file : text; my_line : inout line; file_end : out boolean; dat : out std_logic_vector);

    procedure read_hex_file_to_array (file myfile : text; array_size  : integer; result : inout t_slv8_array);

    -- handling binary files
    type t_binary_file is file of integer;
    type t_binary_file_rec is record
        offset   : integer range 0 to 4;
        long_vec : std_logic_vector(31 downto 0);
    end record;

    procedure init_record(rec : inout t_binary_file_rec);
    procedure read_byte(file my_file  : t_binary_file; byte : out std_logic_vector; rec : inout t_binary_file_rec);
    procedure write_byte(file my_file : t_binary_file; byte : in std_logic_vector; rec : inout t_binary_file_rec);
    procedure purge(file my_file      : t_binary_file; rec : inout t_binary_file_rec);
   
end;

package body tl_file_io_pkg is

    procedure get_char_from_file(file my_file : text; my_line : inout line; file_end : out boolean; hex : out character) is
        variable good : boolean := false;
        variable stop : boolean := false;
    begin
        while not(good) loop
            READ(my_line, hex, good);
            if not(good) then
                if ENDFILE(my_file) then
                    stop := true;
                    hex  := '1';
                    return;
                end if;
                READLINE(my_file, my_line);
            end if;
        end loop;
        file_end := stop;
    end get_char_from_file;

    procedure get_byte_from_file(file my_file : text; my_line : inout line; file_end : out boolean; dat : out std_logic_vector) is
        variable hex  : character;
        variable data : std_logic_vector(7 downto 0);
        variable stop : boolean := false;
    begin
        data := X"00";
        l_1 : loop
            get_char_from_file(my_file, my_line, stop, hex);
            if stop or is_hex_char(hex) then
                exit l_1;
            end if;
        end loop l_1;

        data(3 downto 0) := hex_to_nibble(hex);

        -- see if we can read another good hex char
        get_char_from_file(my_file, my_line, stop, hex);
        if not(stop) and is_hex_char(hex) then
            data(7 downto 4) := data(3 downto 0);
            data(3 downto 0) := hex_to_nibble(hex);
        end if;

        file_end := stop;
        dat      := data;

    end get_byte_from_file;

    procedure read_hex_file_to_array (
        file myfile : text;
        array_size  : integer;
        result      : inout t_slv8_array)
    is
        variable L        : line;
        variable addr     : unsigned(31 downto 0) := (others => '0');
        variable c        : character;
        variable data     : std_logic_vector(7 downto 0);
        variable sum      : unsigned(7 downto 0);
        variable rectype  : std_logic_vector(7 downto 0);
        variable tmp_addr : std_logic_vector(15 downto 0);
        variable fileend  : boolean;
        variable linenr   : integer := 0;
        variable len      : integer;

        variable out_array: t_slv8_array(0 to array_size-1) := (others => (others => '0'));
    begin
      
        outer : while true loop
            if EndFile(myfile) then
                report "Missing end of file record."
                    severity warning;
                exit outer;
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
                        out_array(to_integer(addr)) := data;
                        addr := addr + 1;
                    end loop;
                    
                when X"01" =>           -- end of file record
                    exit outer;
                    
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
                    exit outer;
                    
            end case;

            -- check checksum
            get_byte_from_file(myfile, L, fileend, data);
            assert sum = unsigned(data)
                report "Warning: Checksum incorrect at line: " & integer'image(linenr)
                severity warning;
        end loop;
        result := out_array;

    end read_hex_file_to_array;

    procedure init_record(rec : inout t_binary_file_rec) is
    begin
        rec.offset   := 0;
        rec.long_vec := (others => '0');
    end procedure;

    procedure read_byte(file my_file : t_binary_file; byte : out std_logic_vector; rec : inout t_binary_file_rec) is
        variable i : integer;
    begin
        if rec.offset = 0 then
            read(my_file, i);
            rec.long_vec := std_logic_vector(to_unsigned(i, 32));
        end if;
        byte         := rec.long_vec(7 downto 0);
        rec.long_vec := "00000000" & rec.long_vec(31 downto 8);  -- lsB first
        if rec.offset = 3 then
            rec.offset := 0;
        else
            rec.offset := rec.offset + 1;
        end if;
    end procedure;

    procedure write_byte(file my_file : t_binary_file; byte : in std_logic_vector; rec : inout t_binary_file_rec) is
        variable i : integer;
    begin
        rec.long_vec(31 downto 24) := byte;
        if rec.offset = 3 then
            i          := to_integer(unsigned(rec.long_vec));
            write(my_file, i);
            rec.offset := 0;
        else
            rec.offset   := rec.offset + 1;
            rec.long_vec := "00000000" & rec.long_vec(31 downto 8);  -- lsB first
        end if;
    end procedure;

    procedure purge(file my_file : t_binary_file; rec : inout t_binary_file_rec) is
        variable i : integer;
    begin
        if rec.offset /= 0 then
            i := to_integer(unsigned(rec.long_vec));
            write(my_file, i);
        end if;
    end procedure;

end;

