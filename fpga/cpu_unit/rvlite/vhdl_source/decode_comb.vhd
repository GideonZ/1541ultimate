--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: The block 'decode_comb' is, as the name suggests a combinatorial
--              implementation of a minimal decode stage for RiscV; RV32I+Zicsr.
--              Some illegal combinations are marked as illegal and will cause
--              an illegal instruction trap. Many others are not, just to keep
--              the logic to a minimum.
--------------------------------------------------------------------------------
-- Instruction formats
-- R-type: funct7            rs2(5)  rs1(5) funct3 rd(5)       opcode(7)
-- I-type: imm[11:0]                 rs1(5) funct3 rd(5)       opcode(7)
-- S-type: imm[11:5]         rs2(5)  rs1(5) funct3 imm[4:0]    opcode(7)
-- U-type: imm[31:12]                              rd(5)       opcode(7)
-- J-type: imm[20] imm[10:1] imm[11] imm[19:12]    rd(5)       opcode(7)
-- B-type: imm[12] imm[10:5] rs2(5)  rs1(5) funct3 imm[4:1,11] opcode(7)   
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.core_pkg.all;

entity decode_comb is
generic (
    g_trap_illegal  : boolean := true
);
port (
    program_counter : in  std_logic_vector(31 downto 0) := (others => '0');
    interrupt       : in  std_logic;
    instruction     : in  std_logic_vector(31 downto 0);
    inst_valid      : in  std_logic;
    illegal_inst    : out std_logic;
    decoded         : out t_decode_out
);
end entity;

architecture gideon of decode_comb is
    alias  inst_opcode : std_logic_vector(6 downto 0) is instruction(6 downto 0);
    alias  inst_rd     : std_logic_vector(4 downto 0) is instruction(11 downto 7);
    alias  inst_func3  : std_logic_vector(2 downto 0) is instruction(14 downto 12);
    alias  inst_rs1    : std_logic_vector(4 downto 0) is instruction(19 downto 15);
    alias  inst_rs2    : std_logic_vector(4 downto 0) is instruction(24 downto 20);
    alias  inst_func7  : std_logic_vector(6 downto 0) is instruction(31 downto 25);
    alias  inst_imm12  : std_logic_vector(11 downto 0) is instruction(31 downto 20);

    signal inst_type    : t_instruction_type;
    signal imm_value    : std_logic_vector(31 downto 0);
begin
    process(program_counter, instruction, inst_valid, inst_func3, inst_imm12, inst_opcode, inst_type, imm_value)
        variable opcode_idx   : natural range 0 to 31;

        procedure illegal is
        begin
            illegal_inst <= '1';
            decoded.csr_access  <= '0';
            decoded.reg_write   <= '0';
            decoded.mem_read    <= '0';
            decoded.mem_write   <= '0';
            decoded.flow_ctrl   <= FL_JUMP;
            decoded.flow_target <= TRGT_TRAP;
            decoded.cause       <= "00010";
        end procedure;

        procedure irq is
        begin
            decoded.csr_access  <= '0';
            decoded.reg_write   <= '0';
            decoded.mem_read    <= '0';
            decoded.mem_write   <= '0';
            decoded.flow_ctrl   <= FL_JUMP;
            decoded.flow_target <= TRGT_TRAP;
            decoded.cause       <= "11011";
        end procedure;

    begin
        -- default: handle instruction as nop
        decoded <= c_decoded_nop;
        decoded.valid <= inst_valid;
        decoded.program_counter <= program_counter;
        inst_type <= Itype;
        illegal_inst <= '0';

        -- Memory transfer size is determined by func3
        decoded.alu_operation <= inst_func3;
        case inst_func3 is
        when "000" | "100" => decoded.mem_transfer_size <= BYTE;
        when "001" | "101" => decoded.mem_transfer_size <= HALFWORD;
        when "010" | "110" => decoded.mem_transfer_size <= WORD;
        when "011" | "111" => decoded.mem_transfer_size <= DOUBLE;
        when others => null;
        end case;

        -- Just forward the instruction indices
        decoded.reg_rs1 <= inst_rs1;
        decoded.reg_rs2 <= inst_rs2;
        decoded.reg_rd  <= inst_rd;

        -- the first 5 bits of the opcode are actually relevant for RV32I.
        opcode_idx := to_integer(unsigned(inst_opcode(6 downto 2)));
        case opcode_idx is
        when  0 => -- LOAD
            inst_type <= Itype;
            decoded.reg_rs1_read <= '1';
            decoded.mem_read <= '1';
            decoded.reg_write <= '1';
            decoded.reg_write_sel <= WB_MEM;
            decoded.mem_read_sext <= not inst_func3(2);
            -- mem address is always rs1 + imm

        -- when  3 => -- FENCE
        --    null;

        when  4 => -- ALU_IMM
            inst_type <= Itype;
            decoded.reg_rs1_read <= '1';
            decoded.alu_src_b <= ALU_SRC_IMM;
            if inst_func3 = "101" then
                decoded.alu_func <= inst_func7(5); -- 0x20 => sub / right instead of add / left
            end if;
            decoded.reg_write <= '1';
            decoded.reg_write_sel <= WB_ALU;

        when  5 => -- AUIPC
            inst_type <= Utype;
            decoded.flow_target <= TRGT_REL;
            decoded.reg_write_sel <= WB_REL;
            decoded.reg_write <= '1';

        when  8 => -- STORE
            inst_type <= Stype;
            decoded.reg_rs1_read <= '1';
            decoded.reg_rs2_read <= '1';
            decoded.mem_write <= '1';
            -- mem address is always rs1 + imm

        -- when 11 => -- ATOMIC
        --    null;

        when 12 => -- ALU_REG
            inst_type <= Rtype;
            decoded.alu_src_b <= ALU_SRC_REGB;
            decoded.reg_rs1_read <= '1';
            decoded.reg_rs2_read <= '1';
            decoded.alu_func <= inst_func7(5); -- 0x20 => sub / right instead of add / left
            decoded.reg_write <= '1';

        when 13 => -- Load Upper Immediate (LUI)
            inst_type <= Utype;
            decoded.reg_write <= '1';
            decoded.alu_src_b <= ALU_SRC_IMM;
            decoded.reg_rs1 <= "00000"; -- hack
            decoded.alu_operation <= "110"; -- xor
            decoded.reg_write_sel <= WB_ALU;

        when 24 => -- Branch
            inst_type <= Btype;
            decoded.reg_rs1_read <= '1';
            decoded.reg_rs2_read <= '1';
            decoded.flow_ctrl <= FL_COND;
            decoded.flow_target <= TRGT_REL;

        when 25 => -- JALR
            inst_type <= Itype;
            decoded.reg_write <= '1';
            decoded.reg_write_sel <= WB_PC4;
            decoded.flow_ctrl <= FL_JUMP;
            decoded.flow_target <= TRGT_MEM;
            decoded.reg_rs1_read <= '1';

        when 27 => -- JAL
            inst_type <= Jtype;
            decoded.reg_write <= '1';
            decoded.reg_write_sel <= WB_PC4;
            decoded.flow_ctrl <= FL_JUMP;
            decoded.flow_target <= TRGT_REL;

        when 28 => -- CSR / MRET / ECALL etc
            inst_type <= Itype;
            case inst_func3 is
            when "000" => -- ECALL, EBREAK, MRET
                case inst_imm12 is
                -- when X"000" | X"800" => -- ECALL (or IRQ)
                --     decoded.flow_ctrl   <= FL_JUMP;
                --     decoded.flow_target <= TRGT_TRAP;
                --     decoded.cause  <= inst_imm12(11) & X"B";
                when X"000" => -- ECALL
                    decoded.flow_ctrl   <= FL_JUMP;
                    decoded.flow_target <= TRGT_TRAP;
                    decoded.cause  <= '0' & X"B";
                when X"001" => -- EBREAK
                    decoded.flow_ctrl   <= FL_JUMP;
                    decoded.flow_target <= TRGT_TRAP;
                    decoded.cause  <= '0' & X"3";
                when X"302" => -- MRET
                    decoded.flow_ctrl   <= FL_JUMP;
                    decoded.flow_target <= TRGT_MEPC;
                when X"105" => -- WFI
                    null; -- nop
                when others =>
                    null;
                    -- decoded.illegal <= '1';
                end case;
            when "001" => -- CSRRW (reg)
                decoded.csr_access <= '1';
                decoded.csr_sel <= CSR_REG;
                decoded.csr_action <= CSR_WRITE;
                decoded.reg_write <= '1';
                decoded.reg_write_sel <= WB_CSR;
                decoded.reg_rs1_read <= '1';
            when "010" => -- CSRRS (reg)
                decoded.csr_access <= '1';
                decoded.csr_sel <= CSR_REG;
                decoded.csr_action <= CSR_SET;
                decoded.reg_write <= '1';
                decoded.reg_write_sel <= WB_CSR;
                decoded.reg_rs1_read <= '1';
            when "011" => -- CSRRC (reg)
                decoded.csr_access <= '1';
                decoded.csr_sel <= CSR_REG;
                decoded.csr_action <= CSR_CLR;
                decoded.reg_write <= '1';
                decoded.reg_write_sel <= WB_CSR;
                decoded.reg_rs1_read <= '1';
            when "101" => -- CSRRW (zimm)
                decoded.csr_access <= '1';
                decoded.csr_sel <= CSR_IMM;
                decoded.csr_action <= CSR_WRITE;
                decoded.reg_write <= '1';
                decoded.reg_write_sel <= WB_CSR;
            when "110" => -- CSRRS (zimm)
                decoded.csr_access <= '1';
                decoded.csr_sel <= CSR_IMM;
                decoded.csr_action <= CSR_SET;
                decoded.reg_write <= '1';
                decoded.reg_write_sel <= WB_CSR;
            when "111" => -- CSRRC (zimm)
                decoded.csr_access <= '1';
                decoded.csr_action <= CSR_CLR;
                decoded.csr_sel <= CSR_IMM;
                decoded.reg_write <= '1';
                decoded.reg_write_sel <= WB_CSR;
            when others =>
                null;
            end case;

        when others =>
            if g_trap_illegal then
                illegal;
            end if;
        end case;

        if inst_opcode(1 downto 0) /= "11" then
            if g_trap_illegal then
                illegal;
            end if;
        end if;

        if interrupt = '1' then
            irq;
        end if;

        -- if the input instruction was not valid, the register reads
        -- cannot cause hazards either.
        if inst_valid = '0' then
            decoded.reg_rs1_read <= '0';
            decoded.reg_rs2_read <= '0';
            decoded.reg_write <= '0';
            decoded.csr_access <= '0';
            decoded.mem_read <= '0';
            decoded.mem_write <= '0';
        end if;

        decoded.instruction_type <= inst_type;
        decoded.imm_value        <= imm_value;
    end process;

    p_imm: process(inst_type, instruction)
    begin
        case inst_type is
        when Itype =>
            imm_value(31 downto 11) <= (others => instruction(31));
            imm_value(10 downto 0)  <= instruction(30 downto 20);
        when Stype =>
            imm_value(31 downto 11) <= (others => instruction(31));
            imm_value(10 downto 5)  <= instruction(30 downto 25);
            imm_value(4 downto 0)   <= instruction(11 downto 7);
        when Btype =>
            imm_value(31 downto 12) <= (others => instruction(31));
            imm_value(11)           <= instruction(7);
            imm_value(10 downto 5)  <= instruction(30 downto 25);
            imm_value(4 downto 1)   <= instruction(11 downto 8);
            imm_value(0)            <= '0';
        when Utype =>
            imm_value(31 downto 12) <= instruction(31 downto 12);
            imm_value(11 downto 0)  <= (others => '0');
        when Jtype =>
            imm_value(31 downto 20) <= (others => instruction(31));
            imm_value(19 downto 12) <= instruction(19 downto 12);
            imm_value(11)           <= instruction(20);
            imm_value(10 downto 1)  <= instruction(30 downto 21);
            imm_value(0)            <= '0';
        when others => -- include Rtype
            imm_value <= (others => '0'); -- can also be '-'
        end case;
    end process;
end architecture;
