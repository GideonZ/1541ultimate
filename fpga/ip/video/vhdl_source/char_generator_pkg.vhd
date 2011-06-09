-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Character Generator
-------------------------------------------------------------------------------
-- File       : char_generator_pkg.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Definitions for the video character generator
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package char_generator_pkg is

    type t_chargen_control is record
        clocks_per_line     : unsigned(10 downto 0);
        char_width          : unsigned(2 downto 0);
        char_height         : unsigned(3 downto 0);
        chars_per_line      : unsigned(6 downto 0);
        active_lines        : unsigned(4 downto 0);
        x_on                : unsigned(10 downto 0);
        y_on                : unsigned(8 downto 0);
        pointer             : unsigned(10 downto 0);
        perform_sync        : std_logic;
    end record;

    constant c_chargen_control_init : t_chargen_control := (
        clocks_per_line     => to_unsigned(672, 11),
        char_width          => to_unsigned(0, 3),
        char_height         => to_unsigned(9, 4),
        chars_per_line      => to_unsigned(64, 7), -- 65
        active_lines        => to_unsigned(4, 5),
        x_on                => to_unsigned(152, 11), -- 139
        y_on                => to_unsigned(260, 9),
        pointer             => to_unsigned(0, 11),
        perform_sync        => '0' );

    -- 640x225 (80x25 => 8x9 chars, in 45 C64 chars width)
    constant c_chargen_control_init_orig : t_chargen_control := (
        clocks_per_line     => to_unsigned(896, 11),
        char_width          => to_unsigned(0, 3),
        char_height         => to_unsigned(8, 4),
        chars_per_line      => to_unsigned(80, 7),
        active_lines        => to_unsigned(25, 5),
        x_on                => to_unsigned(190, 11),
        y_on                => to_unsigned(46, 9),
        pointer             => to_unsigned(0, 11),
        perform_sync        => '0' );

    -- 480x200 (80x25 => 6x8 chars, in 45 C64 chars width)
    constant c_chargen_control_init_480 : t_chargen_control := (
        clocks_per_line     => to_unsigned(672, 11),
        char_width          => to_unsigned(6, 3),
        char_height         => to_unsigned(8, 4),
        chars_per_line      => to_unsigned(80, 7),
        active_lines        => to_unsigned(25, 5),
        x_on                => to_unsigned(142, 11),
        y_on                => to_unsigned(48, 9),
        pointer             => to_unsigned(0, 11),
        perform_sync        => '0' );


    constant c_chargen_line_clocks_hi   : unsigned(3 downto 0) := X"0";
    constant c_chargen_line_clocks_lo   : unsigned(3 downto 0) := X"1";
    constant c_chargen_char_width       : unsigned(3 downto 0) := X"2";
    constant c_chargen_char_height      : unsigned(3 downto 0) := X"3";
    constant c_chargen_chars_per_line   : unsigned(3 downto 0) := X"4";
    constant c_chargen_active_lines     : unsigned(3 downto 0) := X"5";
    constant c_chargen_x_on_hi          : unsigned(3 downto 0) := X"6";
    constant c_chargen_x_on_lo          : unsigned(3 downto 0) := X"7";
    constant c_chargen_y_on_hi          : unsigned(3 downto 0) := X"8";
    constant c_chargen_y_on_lo          : unsigned(3 downto 0) := X"9";
    constant c_chargen_pointer_hi       : unsigned(3 downto 0) := X"A";
    constant c_chargen_pointer_lo       : unsigned(3 downto 0) := X"B";
    constant c_chargen_perform_sync     : unsigned(3 downto 0) := X"C";

end package;
