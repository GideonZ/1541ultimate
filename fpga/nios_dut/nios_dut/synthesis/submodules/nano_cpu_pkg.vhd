library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package nano_cpu_pkg is
    -- Instruction bit 14..12: alu operation
    -- Instruction bit 11: when 1, accu is updated
    -- Instruction bit 15: when 0, flags are updated
    -- Instruction Set (bit 10...0) are address when needed
    --                                                                     ALU
    constant c_load     : std_logic_vector(15 downto 11) := X"0" & '1'; -- load  
    constant c_or       : std_logic_vector(15 downto 11) := X"1" & '1'; -- or
    constant c_and      : std_logic_vector(15 downto 11) := X"2" & '1'; -- and
    constant c_xor      : std_logic_vector(15 downto 11) := X"3" & '1'; -- xor
    constant c_add      : std_logic_vector(15 downto 11) := X"4" & '1'; -- add
    constant c_sub      : std_logic_vector(15 downto 11) := X"5" & '1'; -- sub
    constant c_compare  : std_logic_vector(15 downto 11) := X"5" & '0'; -- sub
    constant c_addc     : std_logic_vector(15 downto 11) := X"6" & '1'; -- addc
    constant c_in       : std_logic_vector(15 downto 11) := X"7" & '1'; -- ext


--    constant c_shr      : std_logic_vector(15 downto 11) := X"7" & '1'; -- shr

    -- no update flags
    constant c_store    : std_logic_vector(15 downto 11) := X"8" & '0'; -- xxx
    constant c_load_ind : std_logic_vector(15 downto 11) := X"8" & '1'; -- load
    constant c_store_ind: std_logic_vector(15 downto 11) := X"9" & '0'; -- xxx
    constant c_out      : std_logic_vector(15 downto 11) := X"A" & '0'; -- xxx

    -- Specials    
    constant c_return   : std_logic_vector(15 downto 11) := X"B" & '1'; -- xxx
    constant c_branch   : std_logic_vector(15 downto 14) := "11";

    -- Branches (bit 10..0) are address
    constant c_br_eq    : std_logic_vector(13 downto 11) := "000"; -- zero
    constant c_br_neq   : std_logic_vector(13 downto 11) := "001"; -- not zero
    constant c_br_mi    : std_logic_vector(13 downto 11) := "010"; -- negative
    constant c_br_pl    : std_logic_vector(13 downto 11) := "011"; -- not negative
    constant c_br_always: std_logic_vector(13 downto 11) := "100"; -- always (jump)
    constant c_br_call  : std_logic_vector(13 downto 11) := "101"; -- always (call)
    constant c_br_c     : std_logic_vector(13 downto 11) := "110"; -- carry
    constant c_br_nc    : std_logic_vector(13 downto 11) := "111"; -- not carry

    -- ALU operations
    constant c_alu_load : std_logic_vector(2 downto 0) := "000";
    constant c_alu_or   : std_logic_vector(2 downto 0) := "001";
    constant c_alu_and  : std_logic_vector(2 downto 0) := "010";
    constant c_alu_xor  : std_logic_vector(2 downto 0) := "011";
    constant c_alu_add  : std_logic_vector(2 downto 0) := "100";
    constant c_alu_sub  : std_logic_vector(2 downto 0) := "101";
    constant c_alu_addc : std_logic_vector(2 downto 0) := "110";
    
end;
