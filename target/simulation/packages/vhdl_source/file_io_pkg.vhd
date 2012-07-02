-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : file io package
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: File IO routines
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library std;
use std.textio.all;

package file_io_pkg is
    subtype t_byte is std_logic_vector(7 downto 0);

    function nibbletohex(nibble : std_logic_vector(3 downto 0)) return character;
    function hextonibble(c      : character) return std_logic_vector;
    function ishexchar(c        : character) return boolean;
    procedure vectohex(vec      : std_logic_vector; str : out string);
    function vectohex(vec       : std_logic_vector; len : integer) return string;

    procedure getcharfromfile(file myfile : text; l : inout line; stop : out boolean; hex : out character);
    procedure getbytefromfile(file myfile : text; l : inout line; fileend : out boolean; dat : out t_byte);

    type t_binary_file is file of integer;

    type t_binary_file_handle is record
        offset  : integer range 0 to 4;
        longvec : std_logic_vector(31 downto 0);
    end record;

    constant emptysp              :       string := "                                      ";
    procedure initrecord(rec      : inout t_binary_file_handle);
    procedure readbyte(file f     :       t_binary_file; b : out t_byte; rec : inout t_binary_file_handle);
    procedure writebyte(file f    :       t_binary_file; b : in t_byte; rec : inout t_binary_file_handle);
    procedure purge(file f        :       t_binary_file; rec : inout t_binary_file_handle);
    procedure onoffchar(l         : inout line; ena : std_logic; ch : character);
    procedure onoffchar(l         : inout line; ena : std_logic; ch, alt : character);
    procedure hexout(l            : inout line; ena : std_logic; vec : std_logic_vector; len : integer);
    procedure write_s(variable li : inout line; s : string);
end;


package body file_io_pkg is

    function nibbletohex(nibble : std_logic_vector(3 downto 0)) return character is
        variable r : character := '?';
    begin
        case nibble is
            when "0000" => r := '0';
            when "0001" => r := '1';
            when "0010" => r := '2';
            when "0011" => r := '3';
            when "0100" => r := '4';
            when "0101" => r := '5';
            when "0110" => r := '6';
            when "0111" => r := '7';
            when "1000" => r := '8';
            when "1001" => r := '9';
            when "1010" => r := 'a';
            when "1011" => r := 'b';
            when "1100" => r := 'c';
            when "1101" => r := 'd';
            when "1110" => r := 'e';
            when "1111" => r := 'f';
            when others => r := 'x';
        end case;
        return r;
    end nibbletohex;

    function hextonibble(c : character) return std_logic_vector is
        variable z : std_logic_vector(3 downto 0);
    begin
        case c is
            when '0'    => z := "0000";
            when '1'    => z := "0001";
            when '2'    => z := "0010";
            when '3'    => z := "0011";
            when '4'    => z := "0100";
            when '5'    => z := "0101";
            when '6'    => z := "0110";
            when '7'    => z := "0111";
            when '8'    => z := "1000";
            when '9'    => z := "1001";
            when 'a'    => z := "1010";
            when 'b'    => z := "1011";
            when 'c'    => z := "1100";
            when 'd'    => z := "1101";
            when 'e'    => z := "1110";
            when 'f'    => z := "1111";
            when 'A'    => z := "1010";
            when 'B'    => z := "1011";
            when 'C'    => z := "1100";
            when 'D'    => z := "1101";
            when 'E'    => z := "1110";
            when 'F'    => z := "1111";
            when others => z := "XXXX";
        end case;
        return z;
    end hextonibble;

    function ishexchar(c : character) return boolean is
    begin
        case c is
            when '0'|'1'|'2'|'3'|'4'|'5'|'6'|'7' => return true;
            when '8'|'9'|'a'|'b'|'c'|'d'|'e'|'f' => return true;
            when 'A'|'B'|'C'|'D'|'E'|'F'         => return true;
            when others                          => return false;
        end case;
        return false;
    end ishexchar;

    procedure vectohex(vec : std_logic_vector; str : out string) is
        variable tempvec            : std_logic_vector(str'length * 4 downto 1) := (others => '0');
        variable j                  : integer;
        variable myvec              : std_logic_vector(vec'range);
        variable len, mylow, myhigh : integer;
    begin
        myvec  := vec;
        len    := str'length;
        mylow  := vec'low;
        myhigh := vec'high;

        if vec'length < tempvec'length then
            tempvec(vec'length downto 1) := vec;
        else
            tempvec := vec(vec'low + tempvec'length - 1 downto vec'low);
        end if;
        for i in str'range loop
            j      := (str'right - i) * 4;
            str(i) := nibbletohex(tempvec(j+4 downto j+1));
        end loop;
    end vectohex;

    function vectohex(vec : std_logic_vector; len : integer) return string is
        variable str : string(1 to len);
    begin
        vectohex(vec, str);
        return str;
    end vectohex;

    procedure getcharfromfile(file myfile : text; l : inout line; stop : out boolean; hex : out character) is
        variable good : boolean := false;
    begin
        while not(good) loop
            read(l, hex, good);
            if not(good) then
                if endfile(myfile) then
                    stop := true;
                    hex  := '1';
                    return;
                end if;
                readline(myfile, l);
            end if;
        end loop;
    end getcharfromfile;

    procedure getbytefromfile(file myfile : text; l : inout line; fileend : out boolean; dat : out t_byte) is
        variable hex  : character;
        variable d    : t_byte;
        variable stop : boolean := false;
    begin
        d := x"00";
        l1 : loop
            getcharfromfile(myfile, l, stop, hex);
            if stop or ishexchar(hex) then
                exit l1;
            end if;
        end loop l1;

        d(3 downto 0) := hextonibble(hex);

        -- see if we can read another good hex char
        getcharfromfile(myfile, l, stop, hex);
        if not(stop) and ishexchar(hex) then
            d(7 downto 4) := d(3 downto 0);
            d(3 downto 0) := hextonibble(hex);
        end if;

        fileend := stop;
        dat     := d;

    end getbytefromfile;

    procedure onoffchar(l : inout line; ena : std_logic; ch : character) is
    begin
        if ena = '1' then
            write(l, ch);
        else
            write(l, ' ');
        end if;
    end onoffchar;

    procedure onoffchar(l : inout line; ena : std_logic; ch, alt : character) is
    begin
        if ena = '1' then
            write(l, ch);
        else
            write(l, alt);
        end if;
    end onoffchar;

    procedure hexout(l : inout line; ena : std_logic; vec : std_logic_vector; len : integer) is
    begin
        if ena = '1' then
            write(l, vectohex(vec, len));
        else
            write(l, emptysp(1 to len));
        end if;
    end hexout;

    procedure initrecord(rec : inout binaryfilerec) is
    begin
        rec.offset  := 0;
        rec.longvec := (others => '0');
    end procedure;

    procedure readbyte(file f : binary_file; b : out t_byte; rec : inout binaryfilerec) is
        variable i : integer;
    begin
        if rec.offset = 0 then
            read(f, i);
            rec.longvec := std_logic_vector(to_unsigned(i, 32));
        end if;
        b           := rec.longvec(7 downto 0);
        rec.longvec := "00000000" & rec.longvec(31 downto 8);  -- lsb first
        if rec.offset = 3 then
            rec.offset := 0;
        else
            rec.offset := rec.offset + 1;
        end if;
    end procedure;

    procedure writebyte(file f : binary_file; b : in t_byte; rec : inout binaryfilerec) is
        variable i : integer;
    begin
        rec.longvec(31 downto 24) := b;
        if rec.offset = 3 then
            i          := to_integer(unsigned(rec.longvec));
            write(f, i);
            rec.offset := 0;
        else
            rec.offset  := rec.offset + 1;
            rec.longvec := "00000000" & rec.longvec(31 downto 8);  -- lsb first
        end if;
    end procedure;

    procedure purge(file f : binary_file; rec : inout binaryfilerec) is
        variable i : integer;
    begin
        if rec.offset /= 0 then
            i := to_integer(unsigned(rec.longvec));
            write(f, i);
        end if;
    end procedure;

    procedure write_s(variable li : inout line; s : string) is
    begin
        write(li, s);
    end write_s;

end;

