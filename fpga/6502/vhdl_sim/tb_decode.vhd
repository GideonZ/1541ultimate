library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

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
--    signal s_load_a              :  boolean;    
    signal s_load_x              :  boolean;    
    signal s_load_y              :  boolean;    

    signal clock        : std_logic := '0';
    signal reset        : std_logic := '0';

    type t_state is (fetch, decode, absolute, abs_hi, abs_fix, branch, branch_fix,
                     indir1, indir2, jump_sub, jump, retrn, rmw1, rmw2, vector, startup,
                     zp, zp_idx, zp_indir, push1, push2, push3, pull1, pull2, pull3 );

    signal state        : t_state;
    signal state_idx    : integer range 0 to 31;

    signal opcode       : string(1 to 13);
    signal sync         : std_logic;
    signal dummy_cycle  : std_logic;
    signal latch_dreg   : std_logic;
    signal copy_d2p     : std_logic;
    signal reg_update   : std_logic;
    signal rwn          : std_logic;
    signal vect_addr    : std_logic_vector(3 downto 0);
    signal a16          : std_logic;
    signal a_mux        : t_amux := c_amux_pc;
    signal dout_mux     : t_dout_mux;
    signal pc_oper      : t_pc_oper;
    signal s_oper       : t_sp_oper;
    signal adl_oper     : t_adl_oper;
    signal adh_oper     : t_adh_oper;
    
    signal stop_clock   : boolean := false;
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
    --s_load_a              <= load_a(i_reg);
    s_load_x              <= load_x(i_reg);
    s_load_y              <= load_y(i_reg);

    test: process
        variable ireg : std_logic_vector(7 downto 0);
        variable v_opcode : string(1 to 13);
    begin
        for i in 0 to 255 loop
            ireg  := std_logic_vector(to_unsigned(i, 8));
            v_opcode := opcode_array(i); 
            assert not (v_opcode(4)=' ' and is_illegal(ireg)) report "Function says it's illegal, opcode does not." & v_opcode severity error;
            assert not (v_opcode(4)='*' and not is_illegal(ireg)) report "Opcode says it's illegal, function says it's not." & v_opcode severity error; 
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
                write_str(L, c_shift_sel_str(to_integer(unsigned(sel))));
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
                inst := std_logic_vector(to_unsigned(x*32, 8));
                write(L, VecToHex(inst, 2));
                write_str(L, "   ");
            end loop;
            writeline(output, L);
            
            for y in 0 to 31 loop
                inst := std_logic_vector(to_unsigned(y, 8));
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
                inst := std_logic_vector(to_unsigned(x*32, 8));
                write(L, VecToHex(inst, 2));
                write_str(L, "   ");
            end loop;
            writeline(output, L);
            
            for y in 0 to 31 loop
                inst := std_logic_vector(to_unsigned(y, 8));
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

        procedure write_bool(variable L : inout line; b : boolean; t : string; f : string := "") is
        begin
            write(L, ";");
            if b then
                write(L, t);
            else
                write(L, f);
            end if;          
        end procedure;

        procedure write_signals(variable L : inout line) is
        begin
            write_bool(L, latch_dreg='1', "LatchDREG");
            write_bool(L, reg_update='1', "RegUpdate");
            write_bool(L, copy_d2p='1', "Load P");
            write_bool(L, rwn='0', "Write");
            write_bool(L, a16='1', "Inst", "Data");
            write(L, ";ADDR:" & t_amux'image(a_mux)); 
            write(L, ";DOUT:" & t_dout_mux'image(dout_mux)); 
            write(L, ";PC:" & t_pc_oper'image(pc_oper)); 
            write(L, ";SP:" & t_sp_oper'image(s_oper)); 
            write(L, ";ADL:" & t_adl_oper'image(adl_oper)); 
            write(L, ";ADH:" & t_adh_oper'image(adh_oper)); 
        end procedure;

        file fout : text;
        type t_string_array is array(natural range <>) of string(1 to 3);

        constant alu_strings : t_string_array(0 to 7) := ("OR ", "AND", "EOR", "ADC", "---", "LD ", "CMP", "SBC" );
        constant shift_strings : t_string_array(0 to 7) := ("ASL", "ROL", "LSR", "ROR", "---", "LD ", "DEC", "INC" );
        variable j : integer;         
    begin
        for i in 0 to 255 loop
            inst := std_logic_vector(to_unsigned(i, 8));
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

        reset <= '1';
        wait until clock = '1';
        wait until clock = '1';
        reset <= '0';
        
        file_open(fout, "opcodes.csv", WRITE_MODE);
        
        write(L, "Code;Opcode;State;IMM#;IMPL;ABS;REL;RMW;ZP;INDIR;INDEXED;X/Y;AJMP;JUMP;STACK;PUSH;LOAD;STORE;SHIFT;ALU;ALU->A;->SH" );
        writeline(fout, L);   

        for i in 0 to 256 loop
            j := i mod 256;
            while sync /= '1' loop
                write(L, ";;" & t_state'image(state));
                write(L, ";;;;;;;;;;;;;;;;;;;");
                write_signals(L);
                writeline(fout,L);
                wait until clock = '1';
            end loop;
            i_reg <= std_logic_vector(to_unsigned(j, 8));
            write(L, VecToHex(std_logic_vector(to_unsigned(j, 8)), 2));
            write(L, ";" & opcode_array(j));
            write(L, ";" & t_state'image(state));
            write_bool(L, b_is_immediate(j)  , "IMM#");    
            write_bool(L, b_is_implied(j)    , "IMPL");
            write_bool(L, b_is_absolute(j)   , "ABS");
            write_bool(L, b_is_relative(j)   , "REL");
            write_bool(L, b_is_rmw(j)        , "RMW");
            write_bool(L, b_is_zeropage(j)   , "ZP");
            write_bool(L, b_is_indirect(j)   , "INDIR");
            write_bool(L, b_is_postindexed(j), "INDEXED");
            write_bool(L, b_select_index_y(j), "Y", "X" );
            write_bool(L, b_is_abs_jump(j)   , "AJMP");
            write_bool(L, b_is_jump(j)       , "JUMP");
            write_bool(L, b_is_stack(j)      , "STACK");
            write_bool(L, b_is_push(j) and b_is_stack(j), "PUSH");
            write_bool(L, b_is_load(j)       , "LOAD");
            write_bool(L, b_is_store(j)      , "STORE");
            write_bool(L, b_is_shift(j)      , "SHIFT:" & shift_strings(to_integer(unsigned(i_reg(7 downto 5)))));
            write_bool(L, b_is_alu(j)        , "ALU:" & alu_strings(to_integer(unsigned(i_reg(7 downto 5)))));
            write_bool(L, b_store_a_from_alu(j), "ALU->A");
            write(L, ";");
            output_shift_sel(b_is_shift(j), b_shift_sel(j));
            write_signals(L);
            writeline(fout,L);
            wait until clock = '1';
        end loop;
        stop_clock <= true;
        file_close(fout);
        wait;
    end process;

    clock <= not clock after 0.5 us when not stop_clock;

    i_proc: entity work.proc_control
    generic map (true)
    port map (
        clock        => clock,
        clock_en     => '1',
        ready        => '1',
        reset        => reset,
        interrupt    => '0',
        i_reg        => i_reg,
        index_carry  => '0',
        pc_carry     => '0',
        branch_taken => true,
        sync         => sync,
        dummy_cycle  => open,
        state_idx    => state_idx,
        latch_dreg   => latch_dreg,
        copy_d2p     => copy_d2p,
        reg_update   => reg_update,
        rwn          => rwn,
--        set_i_flag   => set_i_flag,
--        nmi_done     => nmi_done,
        vect_sel     => "00",
        vect_addr    => vect_addr,
        a16          => a16,
        a_mux        => a_mux,
        dout_mux     => dout_mux,
        pc_oper      => pc_oper,
        s_oper       => s_oper,
        adl_oper     => adl_oper,
        adh_oper     => adh_oper
    );

    state <= t_state'val(state_idx);
    opcode <= opcode_array(to_integer(unsigned(i_reg)));
end tb;    

