----------------------------------------------------------------------------------------------
--
--      Input file         : execute.vhd
--      Design name        : execute
--      Author             : Tamar Kranenburg
--      Company            : Delft University of Technology
--                         : Faculty EEMCS, Department ME&CE
--                         : Systems and Circuits group
--
--      Description        : The Execution Unit performs all arithmetic operations and makes
--                           the branch decision. Furthermore the forwarding logic is located
--                           here. Everything is computed within a single clock-cycle
--
--
----------------------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

library mblite;
use mblite.config_Pkg.all;
use mblite.core_Pkg.all;
use mblite.std_Pkg.all;

entity execute is generic
(
    G_USE_HW_MUL : boolean := CFG_USE_HW_MUL;
    G_USE_BARREL : boolean := CFG_USE_BARREL
);
port
(
    exec_o : out execute_out_type;
    exec_i : in execute_in_type;
    ena_i  : in std_logic;
    rst_i  : in std_logic;
    clk_i  : in std_logic
);
end execute;

architecture arch of execute is

    type execute_reg_type is record
        carry               : std_logic;
        break_in_progress   : std_logic;
        flush_ex            : std_logic;
    end record;

    signal r, rin     : execute_out_type;
    signal reg, regin : execute_reg_type;

begin

    exec_o <= r;

    execute_comb: process(exec_i, r, reg)

        variable v : execute_out_type;
        variable v_reg : execute_reg_type;

        variable alu_src_a : std_logic_vector(CFG_DMEM_WIDTH - 1 downto 0);
        variable alu_src_b : std_logic_vector(CFG_DMEM_WIDTH - 1 downto 0);
        variable carry : std_logic;

        variable result : std_logic_vector(CFG_DMEM_WIDTH downto 0);
        variable result_add : std_logic_vector(CFG_DMEM_WIDTH downto 0);
        variable zero : std_logic;

        variable dat_a, dat_b : std_logic_vector(CFG_DMEM_WIDTH - 1 downto 0);
        variable sel_dat_a, sel_dat_b, sel_dat_d : std_logic_vector(CFG_DMEM_WIDTH - 1 downto 0);
        variable mem_result : std_logic_vector(CFG_DMEM_WIDTH - 1 downto 0);
        variable special_reg : std_logic_vector(CFG_DMEM_WIDTH - 1 downto 0);
    begin

        v := r;
        v_reg := reg;
        
        sel_dat_a := select_register_data(exec_i.dat_a, exec_i.fwd_dec_result, forward_condition(exec_i.fwd_dec.reg_write, exec_i.fwd_dec.reg_d, exec_i.reg_a));
        sel_dat_b := select_register_data(exec_i.dat_b, exec_i.fwd_dec_result, forward_condition(exec_i.fwd_dec.reg_write, exec_i.fwd_dec.reg_d, exec_i.reg_b));
        sel_dat_d := select_register_data(exec_i.dat_d, exec_i.fwd_dec_result, forward_condition(exec_i.fwd_dec.reg_write, exec_i.fwd_dec.reg_d, exec_i.ctrl_wrb.reg_d));

        if reg.flush_ex = '1' then
            v.ctrl_mem.mem_write := '0';
            v.ctrl_mem.mem_read := '0';
            v.ctrl_wrb.reg_write := '0';
            v.ctrl_wrb.reg_d := (others => '0');
        else
            v.ctrl_mem := exec_i.ctrl_mem;
            v.ctrl_wrb := exec_i.ctrl_wrb;
            if exec_i.ctrl_wrb.reg_d = "00000" then
                v.ctrl_wrb.reg_write := '0';
            end if;
        end if;

        if exec_i.ctrl_mem_wrb.mem_read = '1' then
            mem_result := align_mem_load(exec_i.mem_result, exec_i.ctrl_mem_wrb.transfer_size, exec_i.alu_result(1 downto 0));
        else
            mem_result := exec_i.alu_result;
        end if;

        if forward_condition(r.ctrl_wrb.reg_write, r.ctrl_wrb.reg_d, exec_i.reg_a) = '1' then
            -- Forward Execution Result to REG a
            dat_a := r.alu_result;
        elsif forward_condition(exec_i.fwd_mem.reg_write, exec_i.fwd_mem.reg_d, exec_i.reg_a) = '1' then
            -- Forward Memory Result to REG a
            dat_a := mem_result;
        else
            -- DEFAULT: value of REG a
            dat_a := sel_dat_a;
        end if;

        if forward_condition(r.ctrl_wrb.reg_write, r.ctrl_wrb.reg_d, exec_i.reg_b) = '1' then
            -- Forward (latched) Execution Result to REG b
            dat_b := r.alu_result;
        elsif forward_condition(exec_i.fwd_mem.reg_write, exec_i.fwd_mem.reg_d, exec_i.reg_b) = '1' then
            -- Forward Memory Result to REG b
            dat_b := mem_result;
        else
            -- DEFAULT: value of REG b
            dat_b := sel_dat_b;
        end if;

        if forward_condition(r.ctrl_wrb.reg_write, r.ctrl_wrb.reg_d, exec_i.ctrl_wrb.reg_d) = '1' then
            -- Forward Execution Result to REG d
            v.dat_d := align_mem_store(r.alu_result, exec_i.ctrl_mem.transfer_size);
        elsif forward_condition(exec_i.fwd_mem.reg_write, exec_i.fwd_mem.reg_d, exec_i.ctrl_wrb.reg_d) = '1' then
            -- Forward Memory Result to REG d
            v.dat_d := align_mem_store(mem_result, exec_i.ctrl_mem.transfer_size);
        else
            -- DEFAULT: value of REG d
            v.dat_d := align_mem_store(sel_dat_d, exec_i.ctrl_mem.transfer_size);
        end if;

        -- In case more than just one special register needs to be supported, a multiplexer can be made here. For now, just MSR.
        special_reg := (31 => reg.carry,
                         3 => reg.break_in_progress,
                         2 => reg.carry,
                         1 => r.interrupt_enable,
                         others => '0' );

        -- Set the first operand of the ALU
        case exec_i.ctrl_ex.alu_src_a is
            when ALU_SRC_PC       => alu_src_a := sign_extend(exec_i.program_counter, '0', 32);
            when ALU_SRC_NOT_REGA => alu_src_a := not dat_a;
            when ALU_SRC_SPR      => alu_src_a := special_reg;
            when others           => alu_src_a := dat_a;
        end case;

        -- Set the second operand of the ALU
        case exec_i.ctrl_ex.alu_src_b is
            when ALU_SRC_IMM      => alu_src_b := exec_i.imm;
            when ALU_SRC_NOT_IMM  => alu_src_b := not exec_i.imm;
            when ALU_SRC_NOT_REGB => alu_src_b := not dat_b;
            when others           => alu_src_b := dat_b;
        end case;

        -- Determine value of carry in
        case exec_i.ctrl_ex.carry is
            when CARRY_ALU   => carry := reg.carry;
            when CARRY_ONE   => carry := '1';
            when CARRY_ARITH => carry := alu_src_a(CFG_DMEM_WIDTH - 1);
            when others      => carry := '0';
        end case;

        result_add := add(alu_src_a, alu_src_b, carry);

        case exec_i.ctrl_ex.alu_op is
            when ALU_ADD    => result := result_add;
            when ALU_OR     => result := '0' & (alu_src_a or alu_src_b);
            when ALU_AND    => result := '0' & (alu_src_a and alu_src_b);
            when ALU_XOR    => result := '0' & (alu_src_a xor alu_src_b);
            when ALU_SHIFT  => result := alu_src_a(0) & carry & alu_src_a(CFG_DMEM_WIDTH - 1 downto 1);
            when ALU_SEXT8  => result := '0' & sign_extend(alu_src_a(7 downto 0), alu_src_a(7), 32);
            when ALU_SEXT16 => result := '0' & sign_extend(alu_src_a(15 downto 0), alu_src_a(15), 32);
            when ALU_MUL =>
                if G_USE_HW_MUL = true then
                    result := '0' & multiply(alu_src_a, alu_src_b);
                else
                    result := (others => '0');
                end if;
            when ALU_BS =>
                if G_USE_BARREL = true then
                    result := '0' & shift(alu_src_a, alu_src_b(4 downto 0), exec_i.imm(10), exec_i.imm(9));
                else
                    result := (others => '0');
                end if;
            when others =>
                result := (others => '0');
                report "Invalid ALU operation" severity FAILURE;
        end case;

        if reg.flush_ex = '0' then
            -- Set carry register
            if exec_i.ctrl_ex.carry_keep = CARRY_KEEP then
                v_reg.carry := reg.carry;
            else
                v_reg.carry := result(CFG_DMEM_WIDTH);
            end if;
    
            -- MSR operations
            case exec_i.ctrl_ex.msr_op is
            when MSR_SET_I =>
                v.interrupt_enable := '1';
            when MSR_CLR_I =>
                v.interrupt_enable := '0';
            when LOAD_MSR =>
                v_reg.break_in_progress := dat_a(3);
                v_reg.carry             := dat_a(2);
                v.interrupt_enable      := dat_a(1);
            when MSR_SET =>
                v_reg.break_in_progress := exec_i.imm(3) or reg.break_in_progress;
                v_reg.carry             := exec_i.imm(2) or reg.carry;
                v.interrupt_enable      := exec_i.imm(1) or r.interrupt_enable;
            when MSR_CLR =>
                v_reg.break_in_progress := not exec_i.imm(3) and reg.break_in_progress;
                v_reg.carry             := not exec_i.imm(2) and reg.carry;
                v.interrupt_enable      := not exec_i.imm(1) and r.interrupt_enable;
            when others =>
                null;
            end case;
        end if;

        zero := is_zero(dat_a);

        -- Overwrite branch condition
        if reg.flush_ex = '1' then
            v.branch := '0';
        else
            -- Determine branch condition
            case exec_i.ctrl_ex.branch_cond is
                when BNC => v.branch := '1';
                when BEQ => v.branch := zero;
                when BNE => v.branch := not zero;
                when BLT => v.branch := dat_a(CFG_DMEM_WIDTH - 1);
                when BLE => v.branch := dat_a(CFG_DMEM_WIDTH - 1) or zero;
                when BGT => v.branch := not (dat_a(CFG_DMEM_WIDTH - 1) or zero);
                when BGE => v.branch := not dat_a(CFG_DMEM_WIDTH - 1);
                when others => v.branch := '0';
            end case;
        end if;

        v.alu_result := result(CFG_DMEM_WIDTH - 1 downto 0);
        -- Handle CMPU and CMP
        if ( exec_i.ctrl_ex.operation = "11" ) then
            v.alu_result(CFG_DMEM_WIDTH - 1) := not result_add(CFG_DMEM_WIDTH); -- unsigned = bit 32 of result
        elsif ( exec_i.ctrl_ex.operation = "01" ) then
            v.alu_result(CFG_DMEM_WIDTH - 1) := (result_add(CFG_DMEM_WIDTH) xor alu_src_a(CFG_DMEM_WIDTH-1) xor alu_src_b(CFG_DMEM_WIDTH-1)); -- signed
        end if;

        v.program_counter := exec_i.program_counter;

        -- Determine flush signals
        v.flush_id := v.branch;
        v_reg.flush_ex := v.branch and not exec_i.ctrl_ex.delay;

        rin   <= v;
        regin <= v_reg;

    end process;

    execute_seq: process(clk_i)
        procedure proc_execute_reset is
        begin
            r.alu_result             <= (others => '0');
            r.dat_d                  <= (others => '0');
            r.branch                 <= '0';
            r.program_counter        <= (others => '0');
            r.flush_id               <= '0';
            r.interrupt_enable       <= '0';
            r.ctrl_mem.mem_write     <= '0';
            r.ctrl_mem.mem_read      <= '0';
            r.ctrl_mem.transfer_size <= WORD;
            r.ctrl_wrb.reg_d         <= (others => '0');
            r.ctrl_wrb.reg_write     <= '0';
            reg.carry                <= '0';
            reg.break_in_progress    <= '0';
            reg.flush_ex             <= '0';
        end procedure proc_execute_reset;
    begin
        if rising_edge(clk_i) then
            if rst_i = '1' then
                proc_execute_reset;
            elsif ena_i = '1' then
                r   <= rin;
                reg <= regin;
            end if;
        end if;
    end process;
end arch;
