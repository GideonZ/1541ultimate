-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006 TECHNOLUTION B.V., GOUDA NL
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
-- Title      : Style guide example package
-- Author     : Jonathan Hofman (jonathan.hofman@technolution.nl) (import only)
-------------------------------------------------------------------------------
-- Description: This file contains type definitions
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library std;
    use std.textio.all;


package tl_string_util_pkg is

-------------------------------------------------------------------------------
-- functions origination from file_io_package (depricated)
-------------------------------------------------------------------------------

    function nibble_to_hex(nibble : std_logic_vector(3 downto 0)) return character; -- depricated

    function hex_to_nibble(c : character) return std_logic_vector;					-- depricated

    function is_hex_char(c : character) return boolean;                             -- depricated

    function vec_to_hex(vec : std_logic_vector; len : integer) return string;		-- depricated

    procedure write_string(variable my_line : inout line; s : string);				-- depricated

-------------------------------------------------------------------------------
-- functions to convert to string
-------------------------------------------------------------------------------
    ---------------------------------------------------------------------------
    -- converts std_logic into a character
    ---------------------------------------------------------------------------
    function chr(sl: std_logic) return character;

    ---------------------------------------------------------------------------
    -- converts a nible into a hex character 
    ---------------------------------------------------------------------------
    function hchr(slv: std_logic_vector) return character;
    function hchr(slv: unsigned) return character;

    ---------------------------------------------------------------------------
    -- converts std_logic into a string (1 to 1)
    ---------------------------------------------------------------------------
    function str(sl: std_logic) return string;

    ---------------------------------------------------------------------------
    -- converts std_logic_vector into a string (binary base)
    ---------------------------------------------------------------------------
    function str(slv: std_logic_vector) return string;

    ---------------------------------------------------------------------------
    -- converts boolean into a string
    ---------------------------------------------------------------------------
    function str(b: boolean) return string;

    ---------------------------------------------------------------------------
    -- converts an integer into a single character
    -- (can also be used for hex conversion and other bases)
    ---------------------------------------------------------------------------
    function chr(int: integer) return character;

    ---------------------------------------------------------------------------
    -- converts integer into string using specified base
    ---------------------------------------------------------------------------
    function str(int: integer; base: integer) return string;

    ---------------------------------------------------------------------------
    -- converts to string, using base 10
    ---------------------------------------------------------------------------
    function str(int: integer) return string;

    ---------------------------------------------------------------------------
    -- convert into a string in hex format
    ---------------------------------------------------------------------------
    function hstr(slv: std_logic_vector) return string;
    function hstr(uns: unsigned) return string;


-------------------------------------------------------------------------------
-- function to convert from string
-------------------------------------------------------------------------------

    ---------------------------------------------------------------------------
    -- converts a character into std_logic
    ---------------------------------------------------------------------------
    function to_std_logic(c: character) return std_logic;

    ---------------------------------------------------------------------------
    -- converts a hex character into std_logic_vector
    ---------------------------------------------------------------------------
    function hchr_to_std_logic_vector(c: character) return std_logic_vector;

    ---------------------------------------------------------------------------
    -- converts a string into a specific type
    ---------------------------------------------------------------------------
    function to_std_logic_vector(s: string) return std_logic_vector;

    ---------------------------------------------------------------------------
    -- converts a hex string into a specific type
    ---------------------------------------------------------------------------
    function hstr_to_std_logic_vector( str : string; len: integer) return std_logic_vector;
    function hstr_to_integer( str : string ) return integer;

-------------------------------------------------------------------------------
-- string manipulation routines
-------------------------------------------------------------------------------
    ---------------------------------------------------------------------------
    -- convert to upper case
    ---------------------------------------------------------------------------
    function to_upper(c: character) return character;
    function to_upper(s: string) return string;

    ---------------------------------------------------------------------------
    -- convert to lower case
    ---------------------------------------------------------------------------
    function to_lower(c: character) return character;
    function to_lower(s: string) return string;

    ---------------------------------------------------------------------------
    -- check if it is an hex character
    ---------------------------------------------------------------------------
	function is_hchr(c : character) return boolean;

    function resize(s: string; size: natural; default: character := ' ') return string;

    ---------------------------------------------------------------------------
    -- Compare function for strings that correctly handles terminators (NUL)
    ---------------------------------------------------------------------------
    function strcmp(a: string; b: string) return boolean;

-------------------------------------------------------------------------------
-- file I/O
-------------------------------------------------------------------------------

    ---------------------------------------------------------------------------
    -- print
    ---------------------------------------------------------------------------
    -- print string to a file and start new line, if no file is specified the
    -- print function will print to stdout
    ---------------------------------------------------------------------------
    procedure print(text: string);
    procedure print(active: boolean; text: string);
    procedure print(file out_file: TEXT; new_string: in  string);
    procedure print(file out_file: TEXT; char: in  character);

    ---------------------------------------------------------------------------
    -- read variable length string from input file
    ---------------------------------------------------------------------------
    procedure str_read(file in_file: TEXT; res_string: out string);

-------------------------------------------------------------------------------
-- character manipulation
-------------------------------------------------------------------------------
   function char_to_std_logic_vector (
        constant my_char : character)
        return std_logic_vector;

end tl_string_util_pkg;


package body tl_string_util_pkg is

    function nibble_to_hex(nibble : std_logic_vector(3 downto 0)) return character is
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
            when "1010" => r := 'A';
            when "1011" => r := 'B';
            when "1100" => r := 'C';
            when "1101" => r := 'D';
            when "1110" => r := 'E';
            when "1111" => r := 'F';
            when others => r := 'X';
        end case;
        return r;
    end nibble_to_hex;

    function hex_to_nibble(c : character) return std_logic_vector is
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
            when 'A'    => z := "1010";
            when 'B'    => z := "1011";
            when 'C'    => z := "1100";
            when 'D'    => z := "1101";
            when 'E'    => z := "1110";
            when 'F'    => z := "1111";
            when 'a'    => z := "1010";
            when 'b'    => z := "1011";
            when 'c'    => z := "1100";
            when 'd'    => z := "1101";
            when 'e'    => z := "1110";
            when 'f'    => z := "1111";
            when others => z := "XXXX";
        end case;
        return z;
    end hex_to_nibble;

    function is_hex_char(c : character) return boolean is
    begin
        case c is
            when '0'|'1'|'2'|'3'|'4'|'5'|'6'|'7' => return true;
            when '8'|'9'|'A'|'B'|'C'|'D'|'E'|'F' => return true;
            when 'a'|'b'|'c'|'d'|'e'|'f'         => return true;
            when others                          => return false;
        end case;
        return false;
    end is_hex_char;

    procedure vec_to_hex(vec : std_logic_vector; str : out string) is
        variable temp_vec             : std_logic_vector(str'length * 4 downto 1) := (others => '0');
        variable j                    : integer;
        variable my_vec               : std_logic_vector(vec'range);
        variable len, my_low, my_high : integer;
    begin
        my_vec  := vec;
        len     := str'length;
        my_low  := vec'low;
        my_high := vec'high;

        if vec'length < temp_vec'length then
            temp_vec(vec'length downto 1) := vec;
        else
            temp_vec := vec(vec'low + temp_vec'length - 1 downto vec'low);
        end if;
        for i in str'range loop
            j      := (str'right - i) * 4;
            str(i) := nibble_to_hex(temp_vec(j+4 downto j+1));
        end loop;
    end vec_to_hex;

    function vec_to_hex(vec : std_logic_vector; len : integer) return string is
        variable str : string(1 to len);
    begin
        vec_to_hex(vec, str);
        return str;
    end vec_to_hex;

    procedure write_string(variable my_line : inout line; s : string) is
    begin
        write(my_line, s);
    end write_string;

-------------------------------------------------------------------------------
-- functions to convert to string
-------------------------------------------------------------------------------
    function chr(sl : std_logic) return character is
        variable c : character;
    begin
        case sl is
            when 'U' => c := 'U';
            when 'X' => c := 'X';
            when '0' => c := '0';
            when '1' => c := '1';
            when 'Z' => c := 'Z';
            when 'W' => c := 'W';
            when 'L' => c := 'L';
            when 'H' => c := 'H';
            when '-' => c := '-';
        end case;
        return c;
    end chr;

    function str(sl : std_logic) return string is
        variable s : string(1 to 1);
    begin
        s(1) := chr(sl);
        return s;
    end str;

    -- converts std_logic_vector into a string (binary base)
    -- (this also takes care of the fact that the range of
    --  a string is natural while a std_logic_vector may
    --  have an integer range)
    function str(slv : std_logic_vector) return string is
        variable result : string (1 to slv'length);
        variable r      : integer;
    begin
        r := 1;
        for i in slv'range loop
            result(r) := chr(slv(i));
            r         := r + 1;
        end loop;
        return result;
    end str;

    function str(b : boolean) return string is
    begin
        if b then
            return "true";
        else
            return "false";
        end if;
    end str;

    -- converts an integer into a character
    -- for 0 to 9 the obvious mapping is used, higher
    -- values are mapped to the characters A-Z
    -- (this is usefull for systems with base > 10)
    -- (adapted from Steve Vogwell's posting in comp.lang.vhdl)

    function chr(int : integer) return character is
        variable c : character;
    begin
        case int is
            when 0      => c := '0';
            when 1      => c := '1';
            when 2      => c := '2';
            when 3      => c := '3';
            when 4      => c := '4';
            when 5      => c := '5';
            when 6      => c := '6';
            when 7      => c := '7';
            when 8      => c := '8';
            when 9      => c := '9';
            when 10     => c := 'A';
            when 11     => c := 'B';
            when 12     => c := 'C';
            when 13     => c := 'D';
            when 14     => c := 'E';
            when 15     => c := 'F';
            when 16     => c := 'G';
            when 17     => c := 'H';
            when 18     => c := 'I';
            when 19     => c := 'J';
            when 20     => c := 'K';
            when 21     => c := 'L';
            when 22     => c := 'M';
            when 23     => c := 'N';
            when 24     => c := 'O';
            when 25     => c := 'P';
            when 26     => c := 'Q';
            when 27     => c := 'R';
            when 28     => c := 'S';
            when 29     => c := 'T';
            when 30     => c := 'U';
            when 31     => c := 'V';
            when 32     => c := 'W';
            when 33     => c := 'X';
            when 34     => c := 'Y';
            when 35     => c := 'Z';
            when others => c := '?';
        end case;
        return c;
    end chr;
    
    function hchr(slv: std_logic_vector) return character is
    begin
         return hchr(unsigned(slv));
    end function;

    function hchr(slv: unsigned) return character is
        variable v_fourbit  : unsigned(3 downto 0);
        variable v_result   : character;
    begin
        v_fourbit := resize(slv, 4);

        case v_fourbit is
        when "0000" => v_result := '0';
        when "0001" => v_result := '1';
        when "0010" => v_result := '2';
        when "0011" => v_result := '3';
        when "0100" => v_result := '4';
        when "0101" => v_result := '5';
        when "0110" => v_result := '6';
        when "0111" => v_result := '7';
        when "1000" => v_result := '8';
        when "1001" => v_result := '9';
        when "1010" => v_result := 'A';
        when "1011" => v_result := 'B';
        when "1100" => v_result := 'C';
        when "1101" => v_result := 'D';
        when "1110" => v_result := 'E';
        when "1111" => v_result := 'F';
        when "ZZZZ" => v_result := 'z';
        when "UUUU" => v_result := 'u';
        when "XXXX" => v_result := 'x';
        when others => v_result := '?';
        end case;
        
        return v_result;
    end function;


    -- convert integer to string using specified base
    -- (adapted from Steve Vogwell's posting in comp.lang.vhdl)
    function str(int : integer; base : integer) return string is
        variable temp    : string(1 to 10);
        variable num     : integer;
        variable abs_int : integer;
        variable len     : integer := 1;
        variable power   : integer := 1;
    begin
        -- bug fix for negative numbers
        abs_int := abs(int);
        num     := abs_int;

        while num >= base loop          -- Determine how many
            len := len + 1;             -- characters required
            num := num / base;          -- to represent the
        end loop;  -- number.

        for i in len downto 1 loop                   -- Convert the number to
            temp(i) := chr(abs_int/power mod base);  -- a string starting
            power   := power * base;                 -- with the right hand
        end loop;  -- side.

        -- return result and add sign if required
        if int < 0 then
            return '-'& temp(1 to len);
        else
            return temp(1 to len);
        end if;

    end str;


    -- convert integer to string, using base 10
    function str(int : integer) return string is
    begin
        return str(int, 10);
    end str;

    -- converts a std_logic_vector into a hex string.
    function hstr(slv : std_logic_vector) return string is
        constant c_hexlen   : integer := (slv'length + 3)/4;
        alias slv_norm      : std_logic_vector(slv'length - 1 downto 0) is slv;

        variable v_longslv  : std_logic_vector(c_hexlen * 4 - 1 downto 0) := (others => '0');
        variable v_result   : string(1 to c_hexlen);
        variable v_fourbit  : std_logic_vector(3 downto 0);
    begin
        v_longslv(slv_norm'range) := slv_norm;

        for i in 0 to c_hexlen - 1 loop
            v_fourbit := v_longslv(((i * 4) + 3) downto (i * 4));

            case v_fourbit is
            when "0000" => v_result(c_hexlen - i) := '0';
            when "0001" => v_result(c_hexlen - i) := '1';
            when "0010" => v_result(c_hexlen - i) := '2';
            when "0011" => v_result(c_hexlen - i) := '3';
            when "0100" => v_result(c_hexlen - i) := '4';
            when "0101" => v_result(c_hexlen - i) := '5';
            when "0110" => v_result(c_hexlen - i) := '6';
            when "0111" => v_result(c_hexlen - i) := '7';
            when "1000" => v_result(c_hexlen - i) := '8';
            when "1001" => v_result(c_hexlen - i) := '9';
            when "1010" => v_result(c_hexlen - i) := 'A';
            when "1011" => v_result(c_hexlen - i) := 'B';
            when "1100" => v_result(c_hexlen - i) := 'C';
            when "1101" => v_result(c_hexlen - i) := 'D';
            when "1110" => v_result(c_hexlen - i) := 'E';
            when "1111" => v_result(c_hexlen - i) := 'F';
            when "ZZZZ" => v_result(c_hexlen - i) := 'z';
            when "UUUU" => v_result(c_hexlen - i) := 'u';
            when "XXXX" => v_result(c_hexlen - i) := 'x';
            when others => v_result(c_hexlen - i) := '?';
            end case;
        end loop;

        return v_result;
    end hstr;

    -- converts an unsigned into a hex string.
    function hstr(uns : unsigned) return string is
    begin
        return hstr(std_logic_vector(uns));
    end;
      
-------------------------------------------------------------------------------
-- function to convert from string
-------------------------------------------------------------------------------
    function to_std_logic(c: character) return std_logic is
        variable sl: std_logic;
    begin
        case c is
            when 'U' => sl := 'U';
            when 'X' => sl := 'X';
            when '0' => sl := '0';
            when '1' => sl := '1';
            when 'Z' => sl := 'Z';
            when 'W' => sl := 'W';
            when 'L' => sl := 'L';
            when 'H' => sl := 'H';
            when '-' => sl := '-';
            when others => sl := 'X';
        end case;

        return sl;
    end to_std_logic;
    
    function hchr_to_std_logic_vector(c: character) return std_logic_vector is
        variable v_result : std_logic_vector(3 downto 0);
    begin
        case c is
            when '0'    => v_result := "0000";
            when '1'    => v_result := "0001";
            when '2'    => v_result := "0010";
            when '3'    => v_result := "0011";
            when '4'    => v_result := "0100";
            when '5'    => v_result := "0101";
            when '6'    => v_result := "0110";
            when '7'    => v_result := "0111";
            when '8'    => v_result := "1000";
            when '9'    => v_result := "1001";
            when 'a'    => v_result := "1010";
            when 'b'    => v_result := "1011";
            when 'c'    => v_result := "1100";
            when 'd'    => v_result := "1101";
            when 'e'    => v_result := "1110";
            when 'f'    => v_result := "1111";
            when 'A'    => v_result := "1010";
            when 'B'    => v_result := "1011";
            when 'C'    => v_result := "1100";
            when 'D'    => v_result := "1101";
            when 'E'    => v_result := "1110";
            when 'F'    => v_result := "1111";
            when others => v_result := "XXXX";
                assert FALSE
                    report "Illegal character "&  c & "in hex string! "
                    severity warning;
        end case;

        return v_result;
    end function;


    function to_std_logic_vector(s: string) return std_logic_vector is
        variable slv: std_logic_vector(s'high-s'low downto 0);
        variable k: integer;
    begin
        k := s'high-s'low;
        for i in s'range loop
            slv(k) := to_std_logic(s(i));
            k      := k - 1;
        end loop;
        return slv;
    end to_std_logic_vector;


    function hstr_to_integer(str: string) return integer is
        variable len    : integer := str'length;
        variable ivalue : integer := 0;
        variable digit  : integer;
    begin
        for i in 1 to len loop
            case to_lower(str(i)) is
                when '0' => digit := 0;
                when '1' => digit := 1;
                when '2' => digit := 2;
                when '3' => digit := 3;
                when '4' => digit := 4;
                when '5' => digit := 5;
                when '6' => digit := 6;
                when '7' => digit := 7;
                when '8' => digit := 8;
                when '9' => digit := 9;
                when 'a' => digit := 10;
                when 'b' => digit := 11;
                when 'c' => digit := 12;
                when 'd' => digit := 13;
                when 'e' => digit := 14;
                when 'f' => digit := 15;
                when others =>
                    assert FALSE
                        report "Illegal character "&  str(i) & "in hex string! "
                        severity ERROR;
            end case;
            ivalue := ivalue * 16 + digit;
        end loop;
        return ivalue;
    end;

    function hstr_to_std_logic_vector(str: string; len: integer) return std_logic_vector is
        variable digit    : std_logic_vector(3 downto 0);
        variable result   : std_logic_vector((str'length * 4) - 1 downto 0);
    begin
        -- we can not use hstr_to_integer and then convert to hex, because the integer range is
        -- limited
        for i in str'range loop
            case to_lower(str(str'length - i + 1)) is
                when '0'    => digit := "0000";
                when '1'    => digit := "0001";
                when '2'    => digit := "0010";
                when '3'    => digit := "0011";
                when '4'    => digit := "0100";
                when '5'    => digit := "0101";
                when '6'    => digit := "0110";
                when '7'    => digit := "0111";
                when '8'    => digit := "1000";
                when '9'    => digit := "1001";
                when 'a'    => digit := "1010";
                when 'b'    => digit := "1011";
                when 'c'    => digit := "1100";
                when 'd'    => digit := "1101";
                when 'e'    => digit := "1110";
                when 'f'    => digit := "1111";
                when others =>
                    assert FALSE
                        report "Illegal character "&  str(i) & "in hex string! "
                        severity error;
            end case;

            result((i * 4) - 1 downto (i - 1) * 4) := digit;
        end loop;

        return result(len - 1 downto 0);
    end;



-------------------------------------------------------------------------------
-- string manipulation routines
-------------------------------------------------------------------------------

    function to_upper(c : character) return character is
        variable u : character;
    begin
        case c is
            when 'a'    => u := 'A';
            when 'b'    => u := 'B';
            when 'c'    => u := 'C';
            when 'd'    => u := 'D';
            when 'e'    => u := 'E';
            when 'f'    => u := 'F';
            when 'g'    => u := 'G';
            when 'h'    => u := 'H';
            when 'i'    => u := 'I';
            when 'j'    => u := 'J';
            when 'k'    => u := 'K';
            when 'l'    => u := 'L';
            when 'm'    => u := 'M';
            when 'n'    => u := 'N';
            when 'o'    => u := 'O';
            when 'p'    => u := 'P';
            when 'q'    => u := 'Q';
            when 'r'    => u := 'R';
            when 's'    => u := 'S';
            when 't'    => u := 'T';
            when 'u'    => u := 'U';
            when 'v'    => u := 'V';
            when 'w'    => u := 'W';
            when 'x'    => u := 'X';
            when 'y'    => u := 'Y';
            when 'z'    => u := 'Z';
            when others => u := c;
        end case;
        return u;
    end to_upper;

    function to_lower(c : character) return character is
        variable l : character;
    begin
        case c is
            when 'A'    => l := 'a';
            when 'B'    => l := 'b';
            when 'C'    => l := 'c';
            when 'D'    => l := 'd';
            when 'E'    => l := 'e';
            when 'F'    => l := 'f';
            when 'G'    => l := 'g';
            when 'H'    => l := 'h';
            when 'I'    => l := 'i';
            when 'J'    => l := 'j';
            when 'K'    => l := 'k';
            when 'L'    => l := 'l';
            when 'M'    => l := 'm';
            when 'N'    => l := 'n';
            when 'O'    => l := 'o';
            when 'P'    => l := 'p';
            when 'Q'    => l := 'q';
            when 'R'    => l := 'r';
            when 'S'    => l := 's';
            when 'T'    => l := 't';
            when 'U'    => l := 'u';
            when 'V'    => l := 'v';
            when 'W'    => l := 'w';
            when 'X'    => l := 'x';
            when 'Y'    => l := 'y';
            when 'Z'    => l := 'z';
            when others => l := c;
        end case;
        return l;
    end to_lower;

    function to_upper(s : string) return string is
        variable uppercase : string (s'range);
    begin
        for i in s'range loop
            uppercase(i) := to_upper(s(i));
        end loop;
        return uppercase;

    end to_upper;

    function to_lower(s : string) return string is
        variable lowercase : string (s'range);
    begin
        for i in s'range loop
            lowercase(i) := to_lower(s(i));
        end loop;
        return lowercase;
    end to_lower;
    
	function is_hchr(c : character) return boolean is
        variable v_result   : boolean;
	begin
        case c is
            when '0'|'1'|'2'|'3'|'4'|'5'|'6'|'7'|
                 '8'|'9'|'A'|'B'|'C'|'D'|'E'|'F'|
                 'a'|'b'|'c'|'d'|'e'|'f'         => v_result := true;
            when others                          => v_result := false;
        end case;
        
        return v_result;
	end function;

    function strcmp(a: string; b: string) return boolean is
        variable r : boolean := true;
    begin
        for i in a'range loop
            if i > b'right then
                if a(i) /= NUL then -- b is shorter and a doesn't terminate here.
                    return false;
                else
                    return true; -- b is shorter, but a does terminate here
                end if;
            end if;                    
            if a(i) /= b(i) then -- characters are not the same
                return false;
            end if;
            if a(i) = NUL and b(i) = NUL then -- a and b have a string terminator at the same place
                                              -- and previous characters were all the same
                return true;
            end if;
        end loop;
        -- if b is longer, then check if b has a terminator
        if (b'right > a'right) then
            if b(a'right + 1) /= NUL then
                return false;
            end if;
        end if;
        return true;
    end strcmp;

-------------------------------------------------------------------------------
-- file I/O
-------------------------------------------------------------------------------
    procedure print(text: string) is
        variable msg_line: line;
    begin
        write(msg_line, text);
        writeline(output, msg_line);
    end print;

    procedure print(active: boolean; text: string)  is
    begin
        if active then
            print(text);
        end if;
    end print;

    procedure print(file out_file: TEXT; new_string: in  string) is
       variable l: line;
    begin
        write(l, new_string);
        writeline(out_file, l);
    end print;

    procedure print(file out_file: TEXT; char: in  character) is
        variable l: line;
    begin
        write(l, char);
        writeline(out_file, l);
    end print;

    procedure str_read(file in_file: TEXT; res_string: out string) is
        variable l:         line;
        variable c:         character;
        variable is_string: boolean;
    begin
        readline(in_file, l);

        -- clear the contents of the result string
        for i in res_string'range loop
            res_string(i) := ' ';
        end loop;
    
        -- read all characters of the line, up to the length
        -- of the results string
        for i in res_string'range loop
            read(l, c, is_string);
            res_string(i) := c;
            if not is_string then -- found end of line
               exit;
            end if;
        end loop;
    end str_read;

    -- appends contents of a string to a file until line feed occurs
    -- (LF is considered to be the end of the string)
    procedure str_write(file out_file: TEXT; new_string: in  string) is
    begin
        for i in new_string'range loop
            print(out_file, new_string(i));
            if new_string(i) = LF then -- end of string
                exit;
            end if;
        end loop;
    end str_write;
    
   function char_to_std_logic_vector (
        constant my_char : character)
        return std_logic_vector is
    begin
        return std_logic_vector(to_unsigned(character'pos(my_char), 8));
    end;

    function resize(s: string; size: natural; default: character := ' ') return string is
        variable result: string(1 to size) := (others => default);
    begin
        if s'length > size then
            result := s(result'range);
        else
            result(s'range) := s;
        end if;

        return result;
    end function;


end tl_string_util_pkg;

