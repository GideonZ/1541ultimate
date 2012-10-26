library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package sampler_pkg is

    type t_sample_state is (idle, playing, finished, fetch1, fetch2);
    type t_sample_mode  is (mono8, mono16);

    type t_voice_state is record
        state       : t_sample_state;
        position    : unsigned(23 downto 0);
        divider     : unsigned(15 downto 0);
        sample_out  : signed(15 downto 0);
    end record;

    constant c_voice_state_init : t_voice_state := (
        state       => idle,
        position    => to_unsigned(0, 24),
        divider     => to_unsigned(0, 16),
        sample_out  => to_signed(0, 16) );
    
    type t_voice_control is record
        enable      : boolean;
        repeat      : boolean;
        interrupt   : boolean;
        interleave  : boolean;
        mode        : t_sample_mode;
        start_addr  : unsigned(25 downto 0);
        repeat_a    : unsigned(23 downto 0);
        repeat_b    : unsigned(23 downto 0);
        length      : unsigned(23 downto 0);        
        rate        : unsigned(15 downto 0);
        volume      : unsigned(5 downto 0);
        pan         : unsigned(3 downto 0); -- 0000 = left, 1111 = right
    end record;

    constant c_voice_control_init : t_voice_control := (
        enable      => false,
        repeat      => false,
        interrupt   => false,
        interleave  => false,
        mode        => mono8,
        start_addr  => to_unsigned(16*1024*1024, 26),
        repeat_a    => to_unsigned(32768, 24),
        repeat_b    => to_unsigned(49152, 24),
        length      => to_unsigned(65536, 24),
        rate        => to_unsigned(283, 16),
        volume      => to_unsigned(63, 6),
        pan         => X"7" );
    
    type t_voice_control_array is array(natural range <>) of t_voice_control;
    type t_voice_state_array   is array(natural range <>) of t_voice_state;
    type t_voice_sample_array  is array(natural range <>) of signed(15 downto 0);
    type t_sample_byte_array   is array(natural range <>) of signed(7 downto 0);
    
    constant c_sample_control       : unsigned(4 downto 0) := '0' & X"0";
    constant c_sample_volume        : unsigned(4 downto 0) := '0' & X"1";
    constant c_sample_pan           : unsigned(4 downto 0) := '0' & X"2";
    constant c_sample_start_addr_h  : unsigned(4 downto 0) := '0' & X"4";
    constant c_sample_start_addr_mh : unsigned(4 downto 0) := '0' & X"5";
    constant c_sample_start_addr_ml : unsigned(4 downto 0) := '0' & X"6";
    constant c_sample_start_addr_l  : unsigned(4 downto 0) := '0' & X"7";
    constant c_sample_length_h      : unsigned(4 downto 0) := '0' & X"9";
    constant c_sample_length_m      : unsigned(4 downto 0) := '0' & X"A";
    constant c_sample_length_l      : unsigned(4 downto 0) := '0' & X"B";
    constant c_sample_rate_h        : unsigned(4 downto 0) := '0' & X"E";
    constant c_sample_rate_l        : unsigned(4 downto 0) := '0' & X"F";
    constant c_sample_rep_a_pos_h   : unsigned(4 downto 0) := '1' & X"1";
    constant c_sample_rep_a_pos_m   : unsigned(4 downto 0) := '1' & X"2";
    constant c_sample_rep_a_pos_l   : unsigned(4 downto 0) := '1' & X"3";
    constant c_sample_rep_b_pos_h   : unsigned(4 downto 0) := '1' & X"5";
    constant c_sample_rep_b_pos_m   : unsigned(4 downto 0) := '1' & X"6";
    constant c_sample_rep_b_pos_l   : unsigned(4 downto 0) := '1' & X"7";
    constant c_sample_clear_irq     : unsigned(4 downto 0) := '1' & X"F";

end package;

