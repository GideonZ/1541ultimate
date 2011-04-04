-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- 
-- Author: Gideon Zweijtzer (gideon.zweijtzer (at) gmail.com)
--
-- Note that this file is copyrighted, and is not supposed to be used in other
-- projects without written permission from the author.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package sid_io_regs_pkg is

    constant c_sid_voices        : unsigned(3 downto 0) := X"0";
    constant c_sid_filter_div    : unsigned(3 downto 0) := X"1";
    constant c_sid_base_left     : unsigned(3 downto 0) := X"2";
    constant c_sid_base_right    : unsigned(3 downto 0) := X"3";
    constant c_sid_snoop_left    : unsigned(3 downto 0) := X"4";
    constant c_sid_snoop_right   : unsigned(3 downto 0) := X"5";
    constant c_sid_enable_left   : unsigned(3 downto 0) := X"6";
    constant c_sid_enable_right  : unsigned(3 downto 0) := X"7";
    constant c_sid_extend_left   : unsigned(3 downto 0) := X"8";
    constant c_sid_extend_right  : unsigned(3 downto 0) := X"9";
    constant c_sid_wavesel_left  : unsigned(3 downto 0) := X"A";
    constant c_sid_wavesel_right : unsigned(3 downto 0) := X"B";
    
    type t_sid_control is record
        base_left       : unsigned(11 downto 4);
        snoop_left      : std_logic;
        enable_left     : std_logic;
        extend_left     : std_logic;
        comb_wave_left  : std_logic;
        
        base_right      : unsigned(11 downto 4);
        snoop_right     : std_logic;
        enable_right    : std_logic;
        extend_right    : std_logic;
        comb_wave_right : std_logic;
    end record;

    constant c_sid_control_init : t_sid_control := (
        base_left       => X"40",
        snoop_left      => '1',
        enable_left     => '1',
        extend_left     => '0',
        comb_wave_left  => '0',
        
        base_right      => X"40",
        snoop_right     => '1',
        enable_right    => '1',
        extend_right    => '0',
        comb_wave_right => '0' );
        
    -- Mapping options are as follows:
    -- STD $D400-$D41F: Snoop='1' Base=$40. Extend='0' (bit 7...1 are significant)
    -- STD $D500-$D51F: Snoop='1' Base=$50. Extend='0'
    -- STD $DE00-$DE1F: Snoop='0' Base=$E0. Extend='0' (bit 4...1 are significant)
    -- STD $DF00-$DF1F: Snoop='0' Base=$F0. Extend='0'
    -- EXT $DF80-$DFFF: Snoop='0' Base=$F8. Extend='1' (bit 4...3 are significant)
    -- .. etc
    
end package;
