library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library work;
use work.pkg_6502_defs.all;
use work.pkg_6502_decode.all;
use work.pkg_6502_opcodes.all;
use work.file_io_pkg.all;

library std;
use std.textio.all;

entity tb_decode is
end tb_decode;

architecture tb of tb_decode is

    signal i_reg        : std_logic_vector(7 downto 0);

    signal s_is_absolute         :  boolean;
    signal s_is_abs_jump         :  boolean;
    signal s_is_immediate        :  boolean;    
    signal s_is_implied          :  boolean;
    signal s_is_stack            :  boolean;
    signal s_is_push             :  boolean;
    signal s_is_zeropage         :  boolean;
    signal s_is_indirect         :  boolean;
    signal s_is_relative         :  boolean;
    signal s_is_load             :  boolean;
    signal s_is_store            :  boolean;
    signal s_is_shift            :  boolean;
    signal s_is_alu              :  boolean;
    signal s_is_rmw              :  boolean;
    signal s_is_jump             :  boolean;
    signal s_is_postindexed      :  boolean;
    signal s_is_illegal          :  boolean;
    signal s_select_index_y      :  boolean;
    signal s_store_a_from_alu    :  boolean;
    signal s_load_a              :  boolean;    
    signal s_load_x              :  boolean;    
    signal s_load_y              :  boolean;    
    signal opcode                : string(1 to 13);

begin

    s_is_absolute         <= is_absolute(i_reg);
    s_is_abs_jump         <= is_abs_jump(i_reg);
    s_is_immediate        <= is_immediate(i_reg);    
    s_is_implied          <= is_implied(i_reg);
    s_is_stack            <= is_stack(i_reg);
    s_is_push             <= is_push(i_reg);
    s_is_zeropage         <= is_zeropage(i_reg);
    s_is_indirect         <= is_indirect(i_reg);
    s_is_relative         <= is_relative(i_reg);
    s_is_load             <= is_load(i_reg);
    s_is_store            <= is_store(i_reg);
    s_is_shift            <= is_shift(i_reg);
    s_is_alu              <= is_alu(i_reg);
    s_is_rmw              <= is_rmw(i_reg);
    s_is_jump             <= is_jump(i_reg);
    s_is_postindexed      <= is_postindexed(i_reg);
    s_is_illegal          <= is_illegal(i_reg);
    s_select_index_y      <= select_index_y(i_reg);
    s_store_a_from_alu    <= store_a_from_alu(i_reg);
    s_load_a              <= load_a(i_reg);
    s_load_x              <= load_x(i_reg);
    s_load_y              <= load_y(i_reg);

    test: process
    begin
        for i in 0 to 255 loop
            i_reg  <= conv_std_logic_vector(i, 8);
            opcode <= opcode_array(i); 
            wait for 1 us;
            assert not (opcode(4)=' ' and s_is_illegal) report "Function says it's illegal, opcode does not." severity error;
            assert not (opcode(4)='*' and not s_is_illegal) report "Opcode says it's illegal, function says it's not." severity error; 
        end loop;        
        wait;
    end process;

    dump: process
        variable inst : std_logic_vector(7 downto 0);
        variable bool : boolean;
        variable L    : line;
        type t_bool_array is array(natural range <>) of boolean;
        type t_sel_array  is array(natural range <>) of std_logic_vector(1 downto 0);
        
        variable b_is_absolute         : t_bool_array(0 to 255);
        variable b_is_abs_jump         : t_bool_array(0 to 255);
        variable b_is_immediate        : t_bool_array(0 to 255);    
        variable b_is_implied          : t_bool_array(0 to 255);
        variable b_is_stack            : t_bool_array(0 to 255);
        variable b_is_push             : t_bool_array(0 to 255);
        variable b_is_zeropage         : t_bool_array(0 to 255);
        variable b_is_indirect         : t_bool_array(0 to 255);
        variable b_is_relative         : t_bool_array(0 to 255);
        variable b_is_load             : t_bool_array(0 to 255);
        variable b_is_store            : t_bool_array(0 to 255);
        variable b_is_shift            : t_bool_array(0 to 255);
        variable b_is_alu              : t_bool_array(0 to 255);
        variable b_is_rmw              : t_bool_array(0 to 255);
        variable b_is_jump             : t_bool_array(0 to 255);
        variable b_is_postindexed      : t_bool_array(0 to 255);
        variable b_select_index_y      : t_bool_array(0 to 255);
        variable b_store_a_from_alu    : t_bool_array(0 to 255);
        variable b_shift_sel           : t_sel_array(0 to 255);

        procedure write_str(variable L : inout line; s : string) is
        begin
            write(L, s);
        end procedure;

        procedure output_bool(bool : boolean) is
        begin
            if bool then
                write_str(L, " (*) ");
            else
                write_str(L, "  .  ");
            end if;
        end procedure;

        procedure output_shift_sel(bool : boolean; sel: std_logic_vector(1 downto 0)) is
            type t_string_array is array(natural range <>) of string(1 to 5);
            constant c_shift_sel_str : t_string_array(0 to 3) := ( "0xFF ", "data ", "accu ", " A&D " );
        begin
            if bool then
                write_str(L, c_shift_sel_str(conv_integer(sel)));
            else
                write_str(L, "  .  ");
            end if;
        end procedure;

        procedure print_table(b : t_bool_array; title: string) is
        begin
            write(L, title);
            writeline(output, L);
            writeline(output, L);
            write_str(L, "    ");
            for x in 0 to 7 loop
                inst := conv_std_logic_vector(x*32, 8);
                write(L, VecToHex(inst, 2));
                write_str(L, "   ");
            end loop;
            writeline(output, L);
            
            for y in 0 to 31 loop
                inst := conv_std_logic_vector(y, 8);
                write(L, VecToHex(inst, 2));
                write(L, ' ');
                for x in 0 to 7 loop
                    output_bool(b(y + x*32));
                end loop;            
                writeline(output, L);
            end loop;
            writeline(output, L);
            writeline(output, L);
        end procedure;

        procedure print_sel_table(title: string) is
        begin
            write(L, title);
            writeline(output, L);
            writeline(output, L);
            write_str(L, "    ");
            for x in 0 to 7 loop
                inst := conv_std_logic_vector(x*32, 8);
                write(L, VecToHex(inst, 2));
                write_str(L, "   ");
            end loop;
            writeline(output, L);
            
            for y in 0 to 31 loop
                inst := conv_std_logic_vector(y, 8);
                write(L, VecToHex(inst, 2));
                write(L, ' ');
                for x in 0 to 7 loop
                    output_shift_sel(b_is_shift(y + x*32), b_shift_sel(y + x*32));
                end loop;            
                writeline(output, L);
            end loop;
            writeline(output, L);
            writeline(output, L);
        end procedure;

    begin
        for i in 0 to 255 loop
            inst := conv_std_logic_vector(i, 8);
            b_is_absolute(i)         := is_absolute(inst);
            b_is_abs_jump(i)         := is_abs_jump(inst);
            b_is_immediate(i)        := is_immediate(inst);    
            b_is_implied(i)          := is_implied(inst);
            b_is_stack(i)            := is_stack(inst);
            b_is_push(i)             := is_push(inst);
            b_is_zeropage(i)         := is_zeropage(inst);
            b_is_indirect(i)         := is_indirect(inst);
            b_is_relative(i)         := is_relative(inst);
            b_is_load(i)             := is_load(inst);
            b_is_store(i)            := is_store(inst);
            b_is_shift(i)            := is_shift(inst);
            b_is_alu(i)              := is_alu(inst);
            b_is_rmw(i)              := is_rmw(inst);
            b_is_jump(i)             := is_jump(inst);
            b_is_postindexed(i)      := is_postindexed(inst);
            b_select_index_y(i)      := select_index_y(inst);
            b_store_a_from_alu(i)    := store_a_from_alu(inst);
            b_shift_sel(i)           := shifter_in_select(inst);
        end loop;

        print_table(b_is_absolute         , "is_absolute");
        print_table(b_is_abs_jump         , "is_abs_jump");
        print_table(b_is_immediate        , "is_immediate");    
        print_table(b_is_implied          , "is_implied");
        print_table(b_is_stack            , "is_stack");
        print_table(b_is_push             , "is_push");
        print_table(b_is_zeropage         , "is_zeropage");
        print_table(b_is_indirect         , "is_indirect");
        print_table(b_is_relative         , "is_relative");
        print_table(b_is_load             , "is_load");
        print_table(b_is_store            , "is_store");
        print_table(b_is_shift            , "is_shift");
        print_table(b_is_alu              , "is_alu");
        print_table(b_is_rmw              , "is_rmw");
        print_table(b_is_jump             , "is_jump");
        print_table(b_is_postindexed      , "is_postindexed");
        print_table(b_select_index_y      , "Select index Y");
        print_table(b_store_a_from_alu    , "Store A from ALU");
        print_sel_table("Shifter Input");
        wait;
    end process;

end tb;    

