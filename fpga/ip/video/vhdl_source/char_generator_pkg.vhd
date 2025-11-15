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
        clocks_per_line     : unsigned(11 downto 0);
        char_width          : unsigned(3 downto 0);
        char_height         : unsigned(4 downto 0);
        chars_per_line      : unsigned(7 downto 0);
        active_lines        : unsigned(5 downto 0);
        x_on                : unsigned(11 downto 0);
        y_on                : unsigned(11 downto 0);
        pointer             : unsigned(14 downto 0);
        perform_sync        : std_logic;
        overlay_on          : std_logic;
        stretch_y           : std_logic;
        big_font            : std_logic;
        transparent         : std_logic_vector(3 downto 0);
    end record;

    constant c_chargen_control_init : t_chargen_control := (
        clocks_per_line     => to_unsigned(672, 12),
        char_width          => to_unsigned(0, 4),
        char_height         => to_unsigned(9, 5),
        chars_per_line      => to_unsigned(60, 8),
        active_lines        => to_unsigned(30, 6),
        x_on                => to_unsigned(15, 12),
        y_on                => to_unsigned(6, 12),
        pointer             => to_unsigned(0, 15),
        perform_sync        => '0',
        overlay_on          => '0',
        stretch_y           => '0',
        big_font            => '0',
        transparent         => X"5" );

    -- 640x225 (80x25 => 8x9 chars, in 45 C64 chars width)
    constant c_chargen_control_init_orig : t_chargen_control := (
        clocks_per_line     => to_unsigned(896, 12),
        char_width          => to_unsigned(0, 4),
        char_height         => to_unsigned(8, 5),
        chars_per_line      => to_unsigned(80, 8),
        active_lines        => to_unsigned(25, 6),
        x_on                => to_unsigned(190, 12),
        y_on                => to_unsigned(46, 12),
        pointer             => to_unsigned(0, 15),
        overlay_on          => '0',
        stretch_y           => '0',
        perform_sync        => '0',
        big_font            => '0',
        transparent         => X"5" );

    -- 480x200 (80x25 => 6x8 chars, in 45 C64 chars width)
    constant c_chargen_control_init_480 : t_chargen_control := (
        clocks_per_line     => to_unsigned(672, 12),
        char_width          => to_unsigned(6, 4),
        char_height         => to_unsigned(8, 5),
        chars_per_line      => to_unsigned(80, 8),
        active_lines        => to_unsigned(25, 6),
        x_on                => to_unsigned(142, 12),
        y_on                => to_unsigned(48, 12),
        pointer             => to_unsigned(0, 15),
        overlay_on          => '0',
        stretch_y           => '0',
        perform_sync        => '0',
        big_font            => '0',
        transparent         => X"5" );

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
    constant c_chargen_transparency     : unsigned(3 downto 0) := X"D";
    constant c_chargen_keyb_row         : unsigned(3 downto 0) := X"E";
    constant c_chargen_keyb_col         : unsigned(3 downto 0) := X"F";

end package;
