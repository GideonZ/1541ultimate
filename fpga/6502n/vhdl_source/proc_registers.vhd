library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.pkg_6502_defs.all;
use work.pkg_6502_decode.all;

-- synthesis translate_off
library std;
use std.textio.all;
--use work.file_io_pkg.all;
-- synthesis translate_on

entity proc_registers is
generic (
    vector_page  : std_logic_vector(15 downto 4) := X"FFF" );
port (
    clock        : in  std_logic;
    clock_en     : in  std_logic;
    ready        : in  std_logic;
    
    reset        : in  std_logic;
                 
    -- package pins
    data_in      : in  std_logic_vector(7 downto 0);
    data_out     : out std_logic_vector(7 downto 0);

    so_n         : in  std_logic := '1';
    
    -- data from "data_oper"
    alu_data     : in  std_logic_vector(7 downto 0);
    mem_data     : in  std_logic_vector(7 downto 0);
    mem_n        : in  std_logic := '0';
    mem_z        : in  std_logic := '0';
    mem_c        : in  std_logic := '0';
    new_flags    : in  std_logic_vector(7 downto 0);
    flags_imm    : in  std_logic;
    
    -- from implied handler
    set_a        : in  std_logic;
    set_x        : in  std_logic;
    set_y        : in  std_logic;
    set_s        : in  std_logic;
    set_data     : in  std_logic_vector(7 downto 0);
    
    -- interrupt pins
    set_i_flag   : in  std_logic;
    vect_addr    : in  std_logic_vector(3 downto 0);
    
    -- from processor state machine and decoder
    sync         : in  std_logic; -- latch ireg
    rwn          : in  std_logic;
    latch_dreg   : in  std_logic;
    vectoring    : in  std_logic;
    reg_update   : in  std_logic;
    flags_update : in  std_logic := '0';
    copy_d2p     : in  std_logic;
    a_mux        : in  t_amux;
    dout_mux     : in  t_dout_mux;
    pc_oper      : in  t_pc_oper;
    s_oper       : in  t_sp_oper;
    adl_oper     : in  t_adl_oper;
    adh_oper     : in  t_adh_oper;

    -- outputs to processor state machine
    i_reg        : out std_logic_vector(7 downto 0) := X"00";
    index_carry  : out std_logic;
    pc_carry     : out std_logic;
    branch_taken : out boolean;

    -- register outputs
    addr_out     : out std_logic_vector(15 downto 0) := X"FFFF";
    
    d_reg        : out std_logic_vector(7 downto 0) := X"00";
    a_reg        : out std_logic_vector(7 downto 0) := X"00";
    x_reg        : out std_logic_vector(7 downto 0) := X"00";
    y_reg        : out std_logic_vector(7 downto 0) := X"00";
    s_reg        : out std_logic_vector(7 downto 0) := X"00";
    p_reg        : out std_logic_vector(7 downto 0) := X"00";
    pc_out       : out std_logic_vector(15 downto 0) );
end proc_registers;

architecture gideon of proc_registers is
    signal dreg         : std_logic_vector(7 downto 0) := X"00";
    signal data_out_i   : std_logic_vector(7 downto 0) := X"00";
    signal a_reg_i      : std_logic_vector(7 downto 0) := X"00";
    signal x_reg_i      : std_logic_vector(7 downto 0) := X"00";
    signal y_reg_i      : std_logic_vector(7 downto 0) := X"00";
    signal selected_idx : std_logic_vector(7 downto 0) := X"00";
    signal i_reg_i      : std_logic_vector(7 downto 0) := X"00";
    signal s_reg_i      : unsigned(7 downto 0) := X"00";
    signal p_reg_i      : std_logic_vector(7 downto 0) := X"30";
    signal pcl, pch     : unsigned(7 downto 0) := X"FF";
    signal adl, adh     : unsigned(7 downto 0) := X"00";
    signal pc_carry_i   : std_logic;
    signal pc_carry_d   : std_logic;
    signal branch_flag  : std_logic;
    signal reg_out      : std_logic_vector(7 downto 0);
    signal h_reg_i      : unsigned(7 downto 0);
    signal ready_d1     : std_logic;
    signal so_d         : std_logic;
    
    alias  C_flag : std_logic is p_reg_i(0);
    alias  Z_flag : std_logic is p_reg_i(1);
    alias  I_flag : std_logic is p_reg_i(2);
    alias  D_flag : std_logic is p_reg_i(3);
    alias  B_flag : std_logic is p_reg_i(4);
    alias  V_flag : std_logic is p_reg_i(6);
    alias  N_flag : std_logic is p_reg_i(7);

    signal p_reg_push   : std_logic_vector(7 downto 0);
    signal adh_clash    : std_logic;
begin
    p_reg_push <= p_reg_i(7 downto 6) & '1' & not vectoring & p_reg_i(3 downto 0);
    
    process(clock)
        variable pcl_t : unsigned(8 downto 0);
        variable adl_t : unsigned(8 downto 0);
        variable v_adh      : unsigned(7 downto 0);
        variable v_reg_sel  : std_logic_vector(1 downto 0);                    
-- synthesis translate_off
        file fout : text;
        variable count : integer := 0;
        variable L : line;
-- synthesis translate_on
    begin
        if rising_edge(clock) then

            if clock_en='1' then
                if flags_imm='1' then
                    p_reg_i <= new_flags;
                end if;

                -- Logic for the crazy instructions that and with adh + 1
                h_reg_i <= X"FF";
                ready_d1 <= ready;
                adh_clash <= '0';
                if ready_d1='1' then
                    --                         $93                           $9E/$9F                            $9C                              $9B
                    if i_reg_i(4 downto 0) = "10011" or i_reg_i(4 downto 1) = "1111" or i_reg_i(4 downto 0) = "11100" or i_reg_i(4 downto 0) = "11011" then
                        h_reg_i <= adh + 1;
                        if i_reg_i(7 downto 5) = "100" then
                            adh_clash <= '1';
                        end if;
                    end if;
                end if; 

                if ready='1' or rwn='0' then
---- synthesis translate_off
--                    if count = 0 then
--                        file_open(fout, "trace.txt", WRITE_MODE);
--                    elsif count = 180000 then
--                        file_close(fout);
--                    elsif count < 180000 then
--                        write(L, "PC:" & VecToHex(pch, 2) & VecToHex(pcl, 2));
--                        write(L, " AD:" & VecToHex(adh, 2) & VecToHex(adl, 2));
--                        write(L, " A:" & VecToHex(a_reg_i, 2));
--                        write(L, " X:" & VecToHex(x_reg_i, 2));
--                        write(L, " Y:" & VecToHex(y_reg_i, 2));
--                        write(L, " S:" & VecToHex(s_reg_i, 2));
--                        write(L, " P:" & VecToHex(p_reg_i, 2));
--                        write(L, " D:" & VecToHex(dreg, 2));
--                        write(L, " DO:" & VecToHex(data_out_i, 2));
--                        write(L, " I:" & VecToHex(i_reg_i, 2));
--                        writeline(fout, L);
--                    end if;
--                    count := count + 1;                    
---- synthesis translate_on
                    -- Data Register
                    if latch_dreg='1' then
                        if rwn = '0' then
                            dreg <= data_out_i;
                        else
                            dreg <= data_in;
                        end if;
                    end if;
                    
                    -- Flags Register
                    if copy_d2p = '1' then
                        p_reg_i <= dreg;
                    elsif reg_update='1' then
                        p_reg_i <= new_flags;
                    elsif flags_update='1' then
                        C_flag <= mem_c;
                        N_flag <= mem_n;
                        Z_flag <= mem_z;
                    end if;
    
                    if set_i_flag='1' then
                        I_flag <= '1';
                    end if;
        
                    so_d <= so_n;        
                    if so_n='0' and so_d = '1' then -- assumed that so_n is synchronous
                        V_flag <= '1';
                    end if;                

                    -- Instruction Register
                    if sync='1' then
                        if vectoring='1' then
                            i_reg_i <= X"00";
                        else
                            i_reg_i <= data_in;
                        end if;
                    end if;
                    
                    -- Logic for the Program Counter
                    pc_carry_i <= '0';
                    case pc_oper is
                    when increment =>
                        if pcl = X"FF" then
                            pch <= pch + 1;
                        end if;
                        pcl <= pcl + 1;
                    
                    when copy =>
                        pcl <= unsigned(dreg);
                        pch <= unsigned(data_in);
                    
                    when from_alu =>
                        pcl_t := ('0' & pcl) + unsigned(dreg(7) & dreg); -- sign extended 1 bit
                        pcl <= pcl_t(7 downto 0);
                        pc_carry_i <= pcl_t(8);
                        pc_carry_d <= dreg(7);
                                    
                    when others => -- keep (and fix)
                        if pc_carry_i='1' then
                            if pc_carry_d='1' then
                                pch <= pch - 1;
                            else
                                pch <= pch + 1;
                            end if;
                        end if;
                    end case;
                                    


                    -- Logic for the Address register
                    case adl_oper is
                    when increment =>
                        adl <= adl + 1;
                    
                    when add_idx =>
                        adl_t := unsigned('0' & dreg) + unsigned('0' & selected_idx);
                        adl <= adl_t(7 downto 0);
                        index_carry <= adl_t(8);
                                        
                    when load_bus =>
                        adl <= unsigned(data_in);
                    
                    when copy_dreg =>
                        adl <= unsigned(dreg);
                        
                    when others =>
                        null;
                    
                    end case;

                    case adh_oper is
                    when increment =>
                        v_adh := adh + 1;
                        if adh_clash = '1' then
                            v_reg_sel := i_reg_i(1 downto 0); 
                            case v_reg_sel is
                            when "00"   => v_adh := v_adh and unsigned(y_reg_i);
                            when "01"   => v_adh := v_adh and unsigned(a_reg_i);
                            when "10"   => v_adh := v_adh and unsigned(x_reg_i);
                            when others => v_adh := v_adh and unsigned(x_reg_i) and unsigned(a_reg_i);
                            end case;
                        end if;
                        adh <= v_adh;
                        
                    when clear =>
                        adh <= (others => '0');
                    
                    when load_bus =>
                        adh <= unsigned(data_in);
                    
                    when others =>
                        null;
                    end case;
                    
                    -- Logic for ALU register
                    if reg_update='1' then
                        if set_a='1' then
                            a_reg_i <= set_data;
                        elsif store_a_from_alu(i_reg_i) then
                            a_reg_i <= alu_data;
                        end if;
                    end if;
                    
                    -- Logic for Index registers
                    if reg_update='1' then
                        if set_x='1' then
                            x_reg_i <= set_data;
                        elsif load_x(i_reg_i) then
                            x_reg_i <= alu_data;
                        end if;
                    end if;
                    
                    if reg_update='1' then
                        if set_y='1' then
                            y_reg_i <= set_data;
                        elsif load_y(i_reg_i) then
                            y_reg_i <= dreg;
                        end if;
                    end if;
        
                    -- Logic for the Stack Pointer
                    if reg_update='1' and set_s='1' then
                        s_reg_i <= unsigned(set_data);
                    else
                        case s_oper is
                        when increment =>
                            s_reg_i <= s_reg_i + 1;
                        
                        when decrement =>
                            s_reg_i <= s_reg_i - 1;
                        
                        when others =>
                            null;
                        end case;
                    end if;
                end if;
            end if;
                        
            -- Reset
            if reset='1' then
                p_reg_i     <= X"34"; -- I=1
                index_carry <= '0';
                so_d        <= '1';
            end if;
        end if;
    end process;

    with i_reg_i(7 downto 6) select branch_flag <=
        N_flag when "00",
        V_flag when "01",
        C_flag when "10",
        Z_flag when "11",
        '0' when others;
    
    branch_taken <= (branch_flag xor not i_reg_i(5))='1';

    with a_mux select addr_out <=
        vector_page & vect_addr     when 0,
        std_logic_vector(adh & adl) when 1,
        X"01" & std_logic_vector(s_reg_i) when 2,
        std_logic_vector(pch & pcl) when 3;
        
    with i_reg_i(1 downto 0) select reg_out <=
        std_logic_vector(h_reg_i) and y_reg_i when "00",
        std_logic_vector(h_reg_i) and a_reg_i when "01",
        std_logic_vector(h_reg_i) and x_reg_i when "10",
        std_logic_vector(h_reg_i) and a_reg_i and x_reg_i when others;

    with dout_mux select data_out_i <=
        dreg when reg_d,
        a_reg_i    when reg_accu,
        reg_out    when reg_axy,
        p_reg_push when reg_flags,
        std_logic_vector(pcl) when reg_pcl,
        std_logic_vector(pch) when reg_pch,
        mem_data   when shift_res,
        X"FF"      when others;

    data_out <= data_out_i;
    selected_idx <= y_reg_i when select_index_y(i_reg_i) else x_reg_i; 
    
    pc_carry <= pc_carry_i;
    s_reg    <= std_logic_vector(s_reg_i);
    p_reg    <= p_reg_i;
    i_reg    <= i_reg_i;
    a_reg    <= a_reg_i;
    x_reg    <= x_reg_i;
    y_reg    <= y_reg_i;
    d_reg    <= dreg;
    pc_out   <= std_logic_vector(pch & pcl);
end gideon;
