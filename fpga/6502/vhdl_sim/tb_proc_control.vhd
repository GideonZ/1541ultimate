library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library work;
use work.pkg_6502_defs.all;
use work.pkg_6502_decode.all;
use work.pkg_6502_opcodes.all;

entity tb_proc_control is
end tb_proc_control;

architecture tb of tb_proc_control is

    signal clock        : std_logic := '0';
    signal clock_en     : std_logic;
    signal reset        : std_logic;
    signal inst         : std_logic_vector(7 downto 0);
    signal force_sync   : std_logic;
    signal index_carry  : std_logic := '1';
    signal pc_carry     : std_logic := '1';
    signal branch_taken : boolean   := true;

    signal latch_dreg   : std_logic;
    signal reg_update   : std_logic;
    signal copy_d2p     : std_logic;
    signal dummy_cycle  : std_logic;
    signal sync         : std_logic;
    signal rwn          : std_logic;
    signal a_mux        : t_amux;
    signal pc_oper      : t_pc_oper;
    signal s_oper       : t_sp_oper;
    signal adl_oper     : t_adl_oper;
    signal adh_oper     : t_adh_oper;
    signal dout_mux     : t_dout_mux;
    signal opcode       : string(1 to 13);

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
    signal s_is_rmw              :  boolean;
    signal s_is_jump             :  boolean;
    signal s_is_postindexed      :  boolean;
    signal s_store_a_from_alu    :  boolean;
    signal s_load_x              :  boolean;
    signal s_load_y              :  boolean;
    
    signal i_reg        : std_logic_vector(7 downto 0);
    signal adh, adl     : std_logic_vector(7 downto 0) := X"00";
    signal pch, pcl     : std_logic_vector(7 downto 0) := X"11";
    signal addr         : std_logic_vector(15 downto 0);
    signal sp           : std_logic_vector(7 downto 0) := X"FF";
    signal dreg         : std_logic_vector(7 downto 0) := X"FF";
    signal databus      : std_logic_vector(7 downto 0);
    signal dout         : std_logic_vector(7 downto 0);
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
    s_is_rmw              <= is_rmw(i_reg);
    s_is_jump             <= is_jump(i_reg);
    s_is_postindexed      <= is_postindexed(i_reg);
    s_store_a_from_alu    <= store_a_from_alu(i_reg);
    s_load_x              <= load_x(i_reg);
    s_load_y              <= load_y(i_reg);
    
    mut: entity work.proc_control
    port map (
        clock        => clock,
        clock_en     => clock_en,
        reset        => reset,
                                    
        interrupt    => '0',
        i_reg        => i_reg,
        index_carry  => index_carry,
        pc_carry     => pc_carry,
        branch_taken => branch_taken,
                                    
        sync         => sync,
        dummy_cycle  => dummy_cycle,
        latch_dreg   => latch_dreg,
        reg_update   => reg_update,
        copy_d2p     => copy_d2p,
        rwn          => rwn,
        a_mux        => a_mux,
        dout_mux     => dout_mux,
        pc_oper      => pc_oper,
        s_oper       => s_oper,
        adl_oper     => adl_oper,
        adh_oper     => adh_oper );

    clock    <= not clock after 50 ns;
    clock_en <= '1';
    reset    <= '1', '0' after 500 ns;

    test: process
    begin
        inst <= X"00";
        force_sync <= '0';
        wait until reset='0';
        for i in 0 to 255 loop
            inst <= conv_std_logic_vector(i, 8);
            wait until sync='1' for 2 us;            
            if sync='0' then
                wait until clock='1';
                force_sync <= '1';
                wait until clock='1';
                force_sync <= '0';
            else
                wait until sync='0';
            end if;
        end loop;        
        wait;
    end process;

    opcode <= opcode_array(conv_integer(i_reg)); 

    process(clock)
    begin
        if rising_edge(clock) and clock_en='1' then
            if latch_dreg='1' then
                dreg <= databus;
            end if;
            
            if sync='1' or force_sync='1' then
                i_reg <= databus;
            end if;

            case pc_oper is
            when increment =>
                if pcl = X"FF" then
                    pch <= pch + 1;
                end if;
                pcl <= pcl + 1;
            
            when copy =>
                pcl <= dreg;
                pch <= databus;
            
            when from_alu =>
                pcl <= pcl + 40;
            
            when others =>
                null;
            end case;
                            
            case adl_oper is
            when increment =>
                adl <= adl + 1;
            
            when add_idx =>
                adl <= adl + 5;
                
            when load_bus =>
                adl <= databus;
            
            when copy_dreg =>
                adl <= dreg;
                
            when others =>
                null;
            
            end case;
            
            case adh_oper is
            when increment =>
                adh <= adh + 1;
            
            when clear =>
                adh <= (others => '0');
            
            when load_bus =>
                adh <= databus;
            
            when others =>
                null;
            end case;
            
            case s_oper is
            when increment =>
                sp <= sp + 1;
            
            when decrement =>
                sp <= sp - 1;
            
            when others =>
                null;
            end case;
        end if;
    end process;

    with a_mux select addr <=
        X"FFFF"    when 0,
        adh & adl  when 1,
        X"01" & sp when 2,
        pch & pcl  when 3;

    with dout_mux select dout <=
        dreg  when reg_d,
        X"11" when reg_axy,
        X"22" when reg_flags,
        pcl   when reg_pcl,
        pch   when reg_pch,
        X"33" when shift_res,
        X"FF" when others;        
        
    databus <= inst when (sync='1' or force_sync='1') else X"D" & addr(3 downto 0);
    
end tb;    
