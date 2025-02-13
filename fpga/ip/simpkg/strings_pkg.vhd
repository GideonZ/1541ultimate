library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library std;
    use std.textio.all;

package strings_pkg is

    ---------------------------------------------------------------------------
    -- convert into a string in hex format
    ---------------------------------------------------------------------------
    function hstr(slv: std_logic_vector) return string;
    function hstr(uns: unsigned) return string;
    function hstr(val: natural) return string;

    ---------------------------------------------------------------------------
    -- converts a hex string into a specific type
    ---------------------------------------------------------------------------
    function hstr_to_std_logic_vector( str : string; len: integer) return std_logic_vector;
    function hstr_to_integer( str : string ) return integer;

end package;


package body strings_pkg is
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
      
    -- converts a value into a hex string.
    function hstr(val : natural) return string is
        variable positions : natural := 1;
        variable test      : natural := val / 16;
    begin
        while test > 0 loop
            positions := positions + 1;
            test := test / 16;
        end loop;
        return hstr(std_logic_vector(to_unsigned(val, 4*positions)));
    end;

    function hstr_to_integer(str: string) return integer is
        variable len    : integer := str'length;
        variable ivalue : integer := 0;
        variable digit  : integer;
    begin
        for i in 1 to len loop
            case str(i) is
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
                when 'a'|'A' => digit := 10;
                when 'b'|'B' => digit := 11;
                when 'c'|'C' => digit := 12;
                when 'd'|'D' => digit := 13;
                when 'e'|'E' => digit := 14;
                when 'f'|'F' => digit := 15;
                when others =>
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
            case str(str'length - i + 1) is
                when '0'     => digit := "0000";
                when '1'     => digit := "0001";
                when '2'     => digit := "0010";
                when '3'     => digit := "0011";
                when '4'     => digit := "0100";
                when '5'     => digit := "0101";
                when '6'     => digit := "0110";
                when '7'     => digit := "0111";
                when '8'     => digit := "1000";
                when '9'     => digit := "1001";
                when 'a'|'A' => digit := "1010";
                when 'b'|'B' => digit := "1011";
                when 'c'|'C' => digit := "1100";
                when 'd'|'D' => digit := "1101";
                when 'e'|'E' => digit := "1110";
                when 'f'|'F' => digit := "1111";
                when others =>
                    report "Illegal character "&  str(i) & "in hex string! "
                    severity error;
            end case;

            result((i * 4) - 1 downto (i - 1) * 4) := digit;
        end loop;

        return result(len - 1 downto 0);
    end;

end package body;
