-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2011 Gideon's Logic Architectures'
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

package copper_pkg is

    constant c_copper_command    : unsigned(3 downto 0) := X"0";
    constant c_copper_status     : unsigned(3 downto 0) := X"1";
    constant c_copper_measure_l  : unsigned(3 downto 0) := X"2";
    constant c_copper_measure_h  : unsigned(3 downto 0) := X"3";
    constant c_copper_framelen_l : unsigned(3 downto 0) := X"4";
    constant c_copper_framelen_h : unsigned(3 downto 0) := X"5";
    constant c_copper_break      : unsigned(3 downto 0) := X"6";

    constant c_copper_cmd_play   : std_logic_vector(3 downto 0) := X"1"; -- starts program in ram
    constant c_copper_cmd_record : std_logic_vector(3 downto 0) := X"2"; -- waits for sync, records all writes until next sync.
    
    constant c_copcode_write_reg : std_logic_vector(7 downto 0) := X"00"; -- starting from 00-2F or so.. takes 1 argument
    constant c_copcode_read_reg  : std_logic_vector(7 downto 0) := X"40"; -- starting from 40-6F or so.. no arguments
    constant c_copcode_wait_irq  : std_logic_vector(7 downto 0) := X"81"; -- waits until falling edge of IRQn
    constant c_copcode_wait_sync : std_logic_vector(7 downto 0) := X"82"; -- waits until sync pulse from timer
    constant c_copcode_timer_clr : std_logic_vector(7 downto 0) := X"83"; -- clears timer 
    constant c_copcode_capture   : std_logic_vector(7 downto 0) := X"84"; -- copies timer to measure register
    constant c_copcode_wait_for  : std_logic_vector(7 downto 0) := X"85"; -- takes a 1 byte argument
    constant c_copcode_wait_until: std_logic_vector(7 downto 0) := X"86"; -- takes 2 bytes argument (wait until timer match)
    constant c_copcode_repeat    : std_logic_vector(7 downto 0) := X"87"; -- restart at program address 0.    
    constant c_copcode_end       : std_logic_vector(7 downto 0) := X"88"; -- ends operation and return to idle
    constant c_copcode_trigger_1 : std_logic_vector(7 downto 0) := X"89"; -- pulses on trigger_1 output
    constant c_copcode_trigger_2 : std_logic_vector(7 downto 0) := X"8A"; -- pulses on trigger_1 output
    
    type t_copper_control is record
        command         : std_logic_vector(3 downto 0);    
        frame_length    : unsigned(15 downto 0);
        stop            : std_logic;
    end record;

    constant c_copper_control_init : t_copper_control := (
        command         => X"0",
        frame_length    => X"4D07", -- 313 lines of 63.. (is incorrect, is one line too long, but to init is fine!)
        stop            => '0' );
        
    type t_copper_status is record
        running         : std_logic;
        measured_time   : unsigned(15 downto 0);
    end record;

    constant c_copper_status_init : t_copper_status := (
        running         => '0',
        measured_time   => X"FFFF" );

end package;
