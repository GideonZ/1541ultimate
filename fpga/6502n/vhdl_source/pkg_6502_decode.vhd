
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package pkg_6502_decode is

    function is_absolute(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_abs_jump(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_immediate(inst: std_logic_vector(7 downto 0)) return boolean;    
    function is_implied(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_stack(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_push(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_zeropage(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_indirect(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_relative(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_load(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_store(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_shift(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_alu(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_rmw(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_jump(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_postindexed(inst: std_logic_vector(7 downto 0)) return boolean;
    function is_illegal(inst: std_logic_vector(7 downto 0)) return boolean;
    function do_bypass_shift(inst : std_logic_vector(7 downto 0)) return std_logic;
    function stack_idx(inst: std_logic_vector(7 downto 0)) return std_logic_vector;
    
    constant c_stack_idx_brk : std_logic_vector(1 downto 0) := "00";
    constant c_stack_idx_jsr : std_logic_vector(1 downto 0) := "01";
    constant c_stack_idx_rti : std_logic_vector(1 downto 0) := "10";
    constant c_stack_idx_rts : std_logic_vector(1 downto 0) := "11";

    function select_index_y (inst: std_logic_vector(7 downto 0)) return boolean;
    function store_a_from_alu (inst: std_logic_vector(7 downto 0)) return boolean;
--    function load_a (inst: std_logic_vector(7 downto 0)) return boolean;
    function load_x (inst: std_logic_vector(7 downto 0)) return boolean;
    function load_y (inst: std_logic_vector(7 downto 0)) return boolean;
    function shifter_in_select (inst: std_logic_vector(7 downto 0)) return std_logic_vector;
    function x_to_alu (inst: std_logic_vector(7 downto 0)) return boolean;
    function affect_registers(inst: std_logic_vector(7 downto 0)) return boolean;

    type t_result_select      is (alu, shift, impl, row0, none);
    function flags_from(inst : std_logic_vector(7 downto 0)) return t_result_select;
end;

package body pkg_6502_decode is

    function is_absolute(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- 4320 = X11X | 1101
        if inst(3 downto 2)="11" then
            return true;
        elsif inst(4 downto 2)="110" and inst(0)='1' then
            return true;
        end if;
        return false;
    end function;

    function is_jump(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        return inst(7 downto 6)="01" and inst(3 downto 0)=X"C" and inst(4) = '0';
    end function;

    function is_immediate(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- 76543210 = 1XX000X0
        if inst(7)='1' and inst(4 downto 2)="000" and inst(0)='0' then
            return true;
        -- 76543210 = XXX010X1
        elsif inst(4 downto 2)="010" and inst(0)='1' then
            return true;
        end if;
        return false;
    end function;
    
    function is_implied(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- 4320 = X100
        return inst(3 downto 0)=X"8" or inst(3 downto 0)=X"A";
    end function;

    function is_stack(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- 76543210
        -- 0xx0x000 => 00, 08, 20, 28, 40, 48, 60, 68
        --             BRK,PHP,JSR,PLP,RTI,PHA,RTS,PLA
        return inst(7)='0' and inst(4)='0' and inst(2 downto 0)="000";
    end function;

    function is_push(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- we already know it's a stack operation, so only the direction is important
        return inst(5)='0';
    end function;

    function is_zeropage(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        if inst(3 downto 2)="01" then
            return true;
        elsif inst(3 downto 2)="00" and inst(0)='1' then
            return true;
        end if;
        return false;
    end function;

    function is_indirect(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        return (inst(3 downto 2)="00" and inst(0)='1');
    end function;

    function is_relative(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        return (inst(4 downto 0)="10000");
    end function;


    function is_store(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        if inst(7 downto 5)="100" then
            if inst(2) = '1' or inst(0) = '1' then
                return true;
            end if;
        end if;
        return false;
    end function;

    function is_shift(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
          -- 0--0101-
        return (inst(7)='0' and inst(4 downto 1) = "0101");
--        -- 16, 1e
--        -- 1--0 10-- => 8[89AB], A[89AB], C[89AB], E[89AB] 
--        if inst(7)='1' and inst(4 downto 2)="010" then
--            return false;
--        end if;
--        return (inst(1)='1');
    end function;

    function is_alu(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
--        if inst(7)='0' and inst(4 downto 1)="0101" then
--            return false;
--        end if;
        return (inst(0)='1');
    end function;

    function is_load(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        return not is_store(inst) and not is_rmw(inst);
    end function;

    function is_rmw(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        return inst(1)='1' and inst(7 downto 6)/="10";
    end function;

    function is_abs_jump(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        return is_jump(inst) and inst(5)='0';
    end function;

    function is_postindexed(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        return inst(4)='1';
    end function;

    function stack_idx(inst: std_logic_vector(7 downto 0)) return std_logic_vector is
    begin
        return inst(6 downto 5);
    end function;

    function select_index_y (inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        if inst(4)='1' and inst(2)='0' and inst(0)='1' then -- XXX1X0X1
            return true;
        elsif inst(7 downto 6)="10" and inst(2 downto 1)="11" then -- 10XXX11X
            return true;
        end if;
        return false;
    end function;        

    function load_a (inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        return (inst = X"68");
    end function;
    
    function store_a_from_alu (inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- 0XXXXXX1 or alu operations "lo"
        -- 1X100001 or alu operations "hi" (except store and cmp)
        -- 0XX01010 (implied) (shift select by 'is_shift'!)
        return (inst(7)='0' and inst(4 downto 0)="01010") or
               (inst(7)='0' and inst(0)='1') or
               (inst(7)='1' and inst(0)='1' and inst(5)='1');
    end function;

    function load_x (inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- 101XXX1X or 1100101-  (for SAX #)
        if inst(7 downto 1)="1100101" then
            return true;
        end if;
        return inst(7 downto 5)="101" and inst(1)='1' and not is_implied(inst);
    end function;

    function load_y (inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- 101XXX00
        return inst(7 downto 5)="101" and inst(1 downto 0)="00" and not is_implied(inst) and not is_relative(inst);
    end function;

    function shifter_in_select (inst: std_logic_vector(7 downto 0)) return std_logic_vector is
    begin
        -- 00 = none, 01 = memory, 10 = A, 11 = A & M
        if inst = X"AB" then -- LAX #$
            return "00"; -- special case
        elsif inst(4 downto 1)="0101" and inst(7)='0' then -- 0xx0101x: 0A, 0B, 2A, 2B, 4A, 4B, 6A, 6B
            return inst(1 downto 0); -- 10 or 11
        end if;
        return "01";
    end function;

    function is_illegal (inst: std_logic_vector(7 downto 0)) return boolean is
        type t_my16bit_array is array(natural range <>) of std_logic_vector(15 downto 0);
        constant c_illegal_map : t_my16bit_array(0 to 15) := (
            X"989C", X"9C9C", X"888C", X"9C9C", X"889C", X"9C9C", X"889C", X"9C9C", 
            X"8A8D", X"D88C", X"8888", X"888C", X"888C", X"9C9C", X"888C", X"9C9C" );
        variable row : std_logic_vector(15 downto 0);
    begin
        row := c_illegal_map(to_integer(unsigned(inst(7 downto 4))));
        return (row(to_integer(unsigned(inst(3 downto 0)))) = '1');
    end function;

    function flags_from(inst : std_logic_vector(7 downto 0)) return t_result_select is
    begin
        -- special case for ANC/ALR
        if inst = X"0B" or inst = X"2B" or inst = X"4B" then
            return shift;
        -- special case for LAS (value and flags are calculated in implied handler)
        elsif inst = X"BB" then
            return impl;
        -- special case for ANE (value and flags are calculated in implied handler, using set A)
        elsif inst = X"8B" then
            return impl;
        elsif is_store(inst) then
            return none;
        elsif inst(0) = '1' then 
            return alu;
        elsif is_shift(inst) then
            return shift;
        elsif (inst(3 downto 0) = X"0" or inst(3 downto 0) = X"4" or inst(3 downto 0) = X"C") and inst(4)='0' then
            return row0;
        elsif is_implied(inst) then
            return impl;
        elsif inst(7 downto 5) = "101" then -- load
            return shift;
        end if;        
        return none;        
    end function;
    
    function x_to_alu (inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        -- 1-00101-  8A,8B,CA,CB
        return inst(5 downto 1)="00101" and inst(7)='1';
    end function;
    
    function affect_registers(inst: std_logic_vector(7 downto 0)) return boolean is
    begin
        if is_implied(inst) then
            return true;
        end if;
        if (inst(7)='0' or inst(6)='1') and inst(2 downto 0) = "110" then
            return false;
        end if;
        -- 7 downto 5 = 000 001 010 011 110 111
        -- 2 downto 0 = 110
        -- should not be true for 06 26 46 66 C6 E6
        -- should not be true for 16 36 56 76 D6 F6
        -- should not be true for 0E 2E 4E 6E CE EE
        -- should not be true for 1E 3E 5E 7E DE FE
        if is_stack(inst) or is_relative(inst) then
            return false;
        end if;
        return true;
    end function;
    
    function do_bypass_shift(inst : std_logic_vector(7 downto 0)) return std_logic is
    begin
        -- may be optimized
        if inst = X"0B" or inst = X"2B" then
            return '1';
        end if;
        return '0';
    end function;
    
end;
