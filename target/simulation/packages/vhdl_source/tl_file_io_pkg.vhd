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
    
    type t_binary_file is file of integer;

    type t_binary_file_rec is record
        offset   : integer range 0 to 4;
        long_vec : std_logic_vector(31 downto 0);
    end record;

    procedure get_char_from_file(file my_file : text; my_line : inout line; file_end : out boolean; hex : out character);
    procedure get_byte_from_file(file my_file : text; my_line : inout line; file_end : out boolean; dat : out std_logic_vector);

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

