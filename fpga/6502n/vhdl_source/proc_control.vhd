
library ieee;
use ieee.std_logic_1164.all;

library work;
use work.pkg_6502_defs.all;
use work.pkg_6502_decode.all;

entity proc_control is
generic (
    g_no_halt    : boolean := false );
port (
    clock        : in  std_logic;
    clock_en     : in  std_logic;
    ready        : in  std_logic;
    reset        : in  std_logic;
                 
    interrupt    : in  std_logic;
    vect_sel     : in  std_logic_vector(2 downto 1);
    i_reg        : in  std_logic_vector(7 downto 0);
    index_carry  : in  std_logic;
    pc_carry     : in  std_logic;
    branch_taken : in  boolean;
    state_idx    : out integer range 0 to 31;  
    sync         : out std_logic;
    dummy_cycle  : out std_logic;
    latch_dreg   : out std_logic;
    copy_d2p     : out std_logic;
    reg_update   : out std_logic;
    flags_update : out std_logic;
    rwn          : out std_logic;
    vect_addr    : out std_logic_vector(3 downto 0);
    nmi_done     : out std_logic;
    set_i_flag   : out std_logic;
    vectoring    : out std_logic;
    a16          : out std_logic;
    taking_intr  : out std_logic;
    a_mux        : out t_amux := c_amux_pc;
    dout_mux     : out t_dout_mux;
    pc_oper      : out t_pc_oper;
    s_oper       : out t_sp_oper;
    adl_oper     : out t_adl_oper;
    adh_oper     : out t_adh_oper );

end proc_control;    


architecture gideon of proc_control is

    type t_state is (fetch, decode, absolute, abs_hi, abs_fix, branch, branch_fix,
                     indir1, indir2, jump_sub, jump, retrn, rmw1, rmw2, vector, startup,
                     zp, zp_idx, zp_indir, push1, push2, push3, pull1, pull2, pull3, pre_irq );
    
    signal state        : t_state;
    signal next_state   : t_state;

    signal rwn_i        : std_logic;
    signal next_cp_p    : std_logic;
    signal next_rwn     : std_logic;
    signal next_dreg    : std_logic;
    signal next_amux    : t_amux;
    signal next_dout    : t_dout_mux;
    signal next_dummy   : std_logic;
    signal next_irqinh  : std_logic;
    signal next_flags   : std_logic;
    signal vectoring_i  : std_logic;
    signal intg_i       : std_logic;
    signal vect_bit_i   : std_logic;
    signal vect_reg     : std_logic_vector(2 downto 1);
    signal sync_i       : std_logic;
    signal irq_inhibit  : std_logic;
    signal trigger_upd  : boolean;
begin
    -- combinatroial process
    process(state, i_reg, index_carry, pc_carry, branch_taken, intg_i, vectoring_i, irq_inhibit)
        variable v_stack_idx : std_logic_vector(1 downto 0);

    begin
        -- defaults
        sync_i     <= '0';
        pc_oper    <= increment;
        next_amux  <= c_amux_pc;
        next_rwn   <= '1';
        next_state <= state;
        adl_oper   <= keep;
        adh_oper   <= keep;
        s_oper     <= keep;
        next_dreg  <= '1';
        next_cp_p  <= '0';
        next_dout  <= reg_d;
        next_dummy <= '0';
        next_irqinh <= '0';
        next_flags  <= '0';
        v_stack_idx := stack_idx(i_reg);

        case state is
        when fetch =>
            if intg_i = '1' then
                pc_oper <= keep;
            else
                pc_oper <= increment;
            end if;
            next_state <= decode;
            sync_i     <= '1';
                        
        when pre_irq =>
            pc_oper    <= keep;
            next_rwn   <= '0';
            next_dreg  <= '0';
            next_dout  <= reg_pch;
            next_state <= push1;
            next_amux  <= c_amux_stack;
            
        when decode =>
            adl_oper   <= load_bus;
            adh_oper   <= clear;

            if is_absolute(i_reg) then
                if is_abs_jump(i_reg) then
                    next_state <= jump;
                else
                    next_state <= absolute;
                end if;
            elsif is_implied(i_reg) then
                pc_oper    <= keep;
                if is_stack(i_reg) then -- PHP, PLP, PHA, PLA
                    next_amux <= c_amux_stack;
                    case v_stack_idx is
                    when "00" => -- PHP
                        next_state <= push3;
                        next_rwn   <= '0';
                        next_dreg  <= '0';
                        next_dout  <= reg_flags;

                    when "10" => -- PHA 
                        next_state <= push3;
                        next_rwn   <= '0';
                        next_dreg <= '0';
                        next_dout <= reg_accu;

                    when others =>
                        next_state <= pull1;
                    end case;
                else
                    next_state <= fetch;
                end if;
            elsif is_zeropage(i_reg) then
                next_amux <= c_amux_addr;
                if is_indirect(i_reg) then
                    if is_postindexed(i_reg) then
                        next_state <= zp_indir;
                    else
                        next_state <= zp;
                        next_dummy <= '1';
                    end if;
                else
                    next_state <= zp;
                    if is_store(i_reg) and not is_postindexed(i_reg) then
                        next_rwn  <= '0';
                        next_dreg <= '0';
                        next_dout <= reg_axy;
                    end if;
                end if;
            elsif is_relative(i_reg) then
                next_state <= branch;
            elsif is_stack(i_reg) then -- non-implied stack operations like BRK, JSR, RTI and RTS
                next_amux <= c_amux_stack;
                case v_stack_idx is
                when c_stack_idx_brk =>
                    if vectoring_i = '1' then
                        pc_oper    <= keep;
                    end if;                    
                    next_rwn   <= '0';
                    next_dreg  <= '0';
                    next_dout  <= reg_pch;
                    next_state <= push1;
                when c_stack_idx_jsr =>
                    next_dreg  <= '0';
                    next_dout  <= reg_pch;
                    next_state <= jump_sub;
                when c_stack_idx_rti =>
                    next_state <= pull1;
                when c_stack_idx_rts =>
                    next_state <= pull2;
                when others =>
                    null;
                end case;
            elsif is_immediate(i_reg) then
                next_state <= fetch;
            elsif g_no_halt then
                next_state <= fetch;
            end if;
                
        when absolute =>
            next_state <= abs_hi;
            next_amux  <= c_amux_addr;
            adh_oper   <= load_bus;
            if is_postindexed(i_reg) then
                adl_oper   <= add_idx;
            elsif not is_zeropage(i_reg) then
                if is_store(i_reg) then
                    next_rwn  <='0';
                    next_dreg <= '0';
                    next_dout <= reg_axy;
                end if;
            end if;
            if is_zeropage(i_reg) then
                pc_oper <= keep;
            else
                pc_oper <= increment;
            end if;

        when abs_hi =>
            pc_oper <= keep;
            if is_postindexed(i_reg) then
                if is_load(i_reg) and index_carry='0' then
                    next_amux  <= c_amux_pc;
                    next_state <= fetch;
                else
                    next_amux  <= c_amux_addr;
                    next_state <= abs_fix;
                    if index_carry='1' then
                        adh_oper <= increment;
                    end if;
                end if;
                if is_store(i_reg) then
                    next_rwn  <= '0';
                    next_dreg <= '0';
                    next_dout <= reg_axy;
                end if;
            else -- not post-indexed  
                if is_jump(i_reg) then
                    next_amux  <= c_amux_addr;
                    next_state <= jump;
                    adl_oper   <= increment;
                elsif is_rmw(i_reg) then
                    next_rwn   <= '0';
                    next_dreg <= '0';
                    next_dout  <= reg_d;
                    next_dummy <= '1';
                    next_state <= rmw1;
                    next_amux  <= c_amux_addr;
                else
                    next_state <= fetch;
                    next_amux  <= c_amux_pc;
                end if;
            end if;

        when abs_fix =>
            pc_oper <= keep;
            
            if is_rmw(i_reg) then
                next_state <= rmw1;
                next_amux  <= c_amux_addr;
                next_rwn   <= '0';
                next_dreg  <= '0';
                next_dout  <= reg_d;
                next_dummy <= '1';
            else
                next_state <= fetch;
                next_amux  <= c_amux_pc;
            end if;

        when branch =>
            next_amux <= c_amux_pc;
            if branch_taken then
                pc_oper    <= from_alu; -- add offset
                next_state <= branch_fix;
            else
                next_state <= decode;
                sync_i     <= '1';
                if intg_i='1' then
                    pc_oper <= keep;
                end if;
            end if;
                            
        when branch_fix =>
            next_amux <= c_amux_pc;

            if pc_carry='1' then
                next_state <= fetch;
                pc_oper    <= keep; -- this will fix the PCH, since the carry is set
            else
                next_state <= decode;
                sync_i     <= '1';
                if intg_i='1' then
                    pc_oper <= keep;
                else
                    pc_oper <= increment;
                end if;
            end if;            

        when indir1 =>
            pc_oper    <= keep;
            next_state <= indir2;
            next_amux  <= c_amux_addr;
            adl_oper   <= copy_dreg;
            adh_oper   <= load_bus;

            if is_store(i_reg) then
                next_rwn  <= '0';
                next_dreg <= '0';
                next_dout <= reg_axy;
            end if;

        when indir2 =>
            pc_oper <= keep;
            if is_rmw(i_reg) then
                next_dummy <= '1';
                next_rwn   <= '0';
                next_dreg  <= '0';
                next_dout  <= reg_d;
                next_state <= rmw1;
                next_amux  <= c_amux_addr;
            else
                next_state <= fetch;
                next_amux  <= c_amux_pc;
            end if;
                
        when jump_sub =>
            next_state <= push1;
            pc_oper    <= keep;
            next_dout  <= reg_pch;
            next_rwn   <= '0';
            next_dreg  <= '0';
            next_amux  <= c_amux_stack;
            
        when jump =>
            pc_oper    <= copy;
            next_amux  <= c_amux_pc;
            if is_stack(i_reg) and v_stack_idx=c_stack_idx_rts then
                next_state <= retrn;
            else
                next_irqinh <= irq_inhibit;
                next_state <= fetch;
            end if;

        when retrn =>
            pc_oper    <= increment;
            next_state <= fetch;
            
        when pull1 =>
            s_oper     <= increment;
            next_state <= pull2;
            next_amux  <= c_amux_stack;
            pc_oper    <= keep;
        
        when pull2 =>
            pc_oper    <= keep;
            if is_implied(i_reg) then
                next_state <= fetch;
                next_amux  <= c_amux_pc;
                next_cp_p  <= not v_stack_idx(1); -- only for PLP
            else -- it was a stack operation, but not implied (RTS/RTI)
                s_oper     <= increment;
                next_state <= pull3;
                next_amux  <= c_amux_stack;
                next_cp_p  <= not v_stack_idx(0); -- only for RTI
            end if;
        
        when pull3 =>
            pc_oper    <= keep;
            s_oper     <= increment;
            next_state <= jump;
            next_amux  <= c_amux_stack;                

        when push1 =>
            pc_oper    <= keep;
            s_oper     <= decrement;
            next_state <= push2;
            next_amux  <= c_amux_stack;
            next_rwn   <= '0';
            next_dreg  <= '0';
            next_dout  <= reg_pcl;
            
        when push2 =>
            pc_oper    <= keep;
            s_oper     <= decrement;
            if v_stack_idx=c_stack_idx_jsr then
                next_state <= jump;
                next_amux  <= c_amux_pc;
            else
                next_state <= push3;
                next_rwn   <= '0';
                next_dreg  <= '0';
                next_dout  <= reg_flags;
                next_amux  <= c_amux_stack;
            end if;
       
        when push3 =>
            pc_oper    <= keep;
            s_oper     <= decrement;
            if is_implied(i_reg) then -- PHP, PHA
                next_amux  <= c_amux_pc;
                next_state <= fetch;
            else
                next_state <= vector;
                next_amux  <= c_amux_vector;
            end if;

        when rmw1 =>
            pc_oper    <= keep;
            next_state <= rmw2;
            next_amux  <= c_amux_addr;
            next_rwn   <= '0';
            next_dreg  <= '1'; -- exception for illegals like SLO
            next_flags <= '1'; 
            next_dout  <= shift_res;
            
        when rmw2 =>
            pc_oper    <= keep;
            next_state <= fetch;
            next_amux  <= c_amux_pc;
            
        when vector =>
            next_state <= jump;
            pc_oper    <= keep;
            next_irqinh <= '1';
            next_amux  <= c_amux_vector;
            
        when startup =>
            next_state <= vector;
            pc_oper    <= keep;
            next_amux  <= c_amux_vector;

        when zp =>
            pc_oper    <= keep;
            if is_postindexed(i_reg) or is_indirect(i_reg) then
                adl_oper   <= add_idx;
                next_state <= zp_idx;
                next_amux  <= c_amux_addr;
                if is_postindexed(i_reg) and is_store(i_reg) then
                    next_rwn  <= '0';
                    next_dreg <= '0';
                    next_dout <= reg_axy;
                end if;
            elsif is_rmw(i_reg) then
                next_dummy <= '1';
                next_state <= rmw1;
                next_amux  <= c_amux_addr;
                next_rwn   <= '0';
                next_dreg  <= '0';
                next_dout  <= reg_d;
            else
                next_state <= fetch;
                next_amux  <= c_amux_pc;
            end if;                
            
        when zp_idx =>
            pc_oper    <= keep;
            if is_indirect(i_reg) then
                next_state <= indir1;
                adl_oper   <= increment;
                next_amux  <= c_amux_addr;
            elsif is_rmw(i_reg) then
                next_state <= rmw1;
                next_amux  <= c_amux_addr;
                next_rwn   <= '0';
                next_dreg  <= '0';
                next_dout  <= reg_d;
            else
                next_state <= fetch;
                next_amux  <= c_amux_pc;
            end if;
                
        when zp_indir =>
            pc_oper    <= keep;
            next_state <= absolute;
            next_amux  <= c_amux_addr;
            adl_oper   <= increment;
            
        when others =>
            null;
        end case;
    end process;
    
    reg_update <= '1' when (state = fetch) and trigger_upd else '0';
                           
    nmi_done   <= not vect_reg(2) when state = vector else '0'; -- Only for NMIs
    set_i_flag <= '1' when state = vector else '0';
    vect_bit_i <= '0' when state = vector else '1';

    vect_addr  <= '1' & vect_reg & vect_bit_i;
    
    process(clock)
    begin
        if rising_edge(clock) then
            if clock_en='1' then
                if next_state = branch or next_state = fetch then
                    if interrupt = '1' and next_irqinh = '0' then                        
                        intg_i <= '1';
                    end if;
                elsif state = vector and ready = '1' then
                    intg_i <= '0';
                end if;

                if ready='1' or rwn_i='0' then
                    state        <= next_state;
                    a_mux        <= next_amux;
                    dout_mux     <= next_dout;
                    rwn_i        <= next_rwn;
                    latch_dreg   <= next_dreg; -- and next_rwn;
                    copy_d2p     <= next_cp_p;
                    dummy_cycle  <= next_dummy;
                    irq_inhibit  <= next_irqinh;
                    flags_update <= next_flags;
                    trigger_upd  <= affect_registers(i_reg);
                                    
                    if sync_i='1' and intg_i='1' then
                        vectoring_i <= '1';
                    elsif state = vector then
                        vectoring_i <= '0';
                    end if;

                    if next_amux = c_amux_vector or next_amux = c_amux_pc then
                        a16 <= '1';
                    else
                        a16 <= '0';
                    end if;
                        
                    if next_state = vector then
                        vect_reg <= vect_sel;
                    end if;                    
                end if;
            end if;
            if reset='1' then
                a16         <= '1';
                state       <= startup; --vector;
                vect_reg    <= vect_sel;
                a_mux       <= c_amux_vector;
                rwn_i       <= '1';
                latch_dreg  <= '1';
                dout_mux    <= reg_d;
                copy_d2p    <= '0';
                vectoring_i <= '0';
                dummy_cycle <= '0';
                irq_inhibit <= '0';
                flags_update  <= '0';
                intg_i <= '0';
            end if;
        end if;    
    end process;

    vectoring <= intg_i; --vectoring_i;
    sync      <= sync_i;
    rwn       <= rwn_i;
    state_idx <= t_state'pos(state);
    
    taking_intr <= vectoring_i;

end architecture;
