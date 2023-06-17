--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: Package with constants and type definitions used in this RiscV
--              implementation.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package core_pkg is

    constant C_8_ZEROS  : std_logic_vector ( 7 downto 0) := (others => '0');
    constant C_16_ZEROS : std_logic_vector (15 downto 0) := (others => '0');
    constant C_24_ZEROS : std_logic_vector (23 downto 0) := (others => '0');
    constant C_32_ZEROS : std_logic_vector (31 downto 0) := (others => '0');

    ----------------------------------------------------------------------------------------------
    -- Types used in rvlite
    ----------------------------------------------------------------------------------------------

    type t_instruction_type is (Rtype, Itype, Stype, Btype, Utype, Jtype);
    type t_alu_operation    is (ALU_ADDSUB, ALU_SH_LEFT, ALU_LESSTHAN, ALU_LESSTHAN_U, ALU_XOR, ALU_SH_RIGHT, ALU_OR, ALU_AND );
    type t_branch_condition is (BEQ, BNE, NOP, BRA, BLT, BGE, BLTU, BGEU);
    type t_src_type_b       is (ALU_SRC_REGB, ALU_SRC_IMM );
    type t_reg_write_sel    is (WB_MEM, WB_ALU, WB_PC4, WB_CSR, WB_REL );
    type t_flow_ctrl        is (FL_NEXT, FL_JUMP, FL_COND);
    type t_flow_target      is (TRGT_REL, TRGT_MEM, TRGT_TRAP, TRGT_MEPC );
    type t_transfer_size    is (BYTE, HALFWORD, WORD, DOUBLE);
    type t_csr_action       is (CSR_NONE, CSR_WRITE, CSR_SET, CSR_CLR);
    type t_csr_input_sel    is (CSR_REG, CSR_IMM);
    
    type t_fetch_in is record
        branch        : std_logic;
        branch_target : std_logic_vector(31 downto 0);
    end record;

    type t_fetch_out is record
        program_counter : std_logic_vector(31 downto 0);
        instruction     : std_logic_vector(31 downto 0);
        inst_valid      : std_logic;
    end record;

    type t_decode_out is record
        valid               : std_logic;
        program_counter     : std_logic_vector(31 downto 0);
        instruction_type    : t_instruction_type;

        reg_rs1             : std_logic_vector(4 downto 0);
        reg_rs1_read        : std_logic;
        reg_rs2             : std_logic_vector(4 downto 0);
        reg_rs2_read        : std_logic;
        reg_rd              : std_logic_vector(4 downto 0);
        reg_write           : std_logic;
        reg_write_sel       : t_reg_write_sel;

        alu_src_b           : t_src_type_b;
        alu_operation       : std_logic_vector(2 downto 0);
        alu_func            : std_logic;

        mem_transfer_size   : t_transfer_size;
        mem_read            : std_logic;
        mem_write           : std_logic;
        mem_read_sext       : std_logic;

        csr_access          : std_logic;
        csr_action          : t_csr_action;
        csr_sel             : t_csr_input_sel;

        flow_ctrl           : t_flow_ctrl;
        flow_target         : t_flow_target;
        cause               : std_logic_vector(4 downto 0);

        imm_value           : std_logic_vector(31 downto 0);
    end record;

    constant c_decoded_nop : t_decode_out := (
        valid               => '0',
        program_counter     => X"00000000",
        instruction_type    => Itype,
        reg_rs1             => "00000",
        reg_rs1_read        => '0',
        reg_rs2             => "00000",
        reg_rs2_read        => '0',
        reg_rd              => "00000",
        reg_write           => '0',
        reg_write_sel       => WB_ALU,
        alu_src_b           => ALU_SRC_REGB,
        alu_operation       => "000",
        alu_func            => '0',
        csr_access          => '0',
        csr_action          => CSR_NONE,
        csr_sel             => CSR_IMM,
        mem_transfer_size   => BYTE,
        mem_read            => '0',
        mem_write           => '0',
        mem_read_sext       => '0',
        flow_ctrl           => FL_NEXT,
        flow_target         => TRGT_MEM,
        cause               => "00000",
        imm_value           => X"00000000"
    );

    type t_execute_out is record
        valid               : std_logic;
        program_counter     : std_logic_vector(31 downto 0);
        do_jump             : std_logic;
        target_pc           : std_logic_vector(31 downto 0);
        alu_result          : std_logic_vector(31 downto 0);
        mem_transfer_size   : t_transfer_size;
        mem_read_sext       : std_logic;
        mem_offset          : std_logic_vector(1 downto 0);
        reg_rd              : std_logic_vector(4 downto 0);
        reg_write           : std_logic;
        reg_write_sel       : t_reg_write_sel;
        csr_data            : std_logic_vector(31 downto 0);
        pc_plus4            : std_logic_vector(31 downto 0);
        rel_address         : std_logic_vector(31 downto 0);
    end record;

    constant c_execute_nop : t_execute_out := (
        valid               => '0',
        program_counter     => X"00000000",
        do_jump             => '0',
        target_pc           => X"00000000",
        alu_result          => X"00000000",
        mem_transfer_size   => BYTE,
        mem_read_sext       => '0',
        mem_offset          => "00",
        reg_rd              => "00000",
        reg_write           => '0',
        reg_write_sel       => WB_ALU,
        csr_data            => X"00000000",
        rel_address         => X"00000000",
        pc_plus4            => X"00000004"
    );
    
    type t_trap is record
        trap                : std_logic;
        mret                : std_logic;
        cause               : std_logic_vector(4 downto 0);
        program_counter     : std_logic_vector(31 downto 0);
    end record;

    type t_csr_req is record
        address     : std_logic_vector(11 downto 0);
        wdata       : std_logic_vector(31 downto 0);
        oper        : t_csr_action;
        enable      : std_logic;
        trap        : t_trap;
        inhibit_irq : std_logic;
    end record;

    type t_csr_resp is record
        rdata       : std_logic_vector(31 downto 0);
        mtvec       : std_logic_vector(31 downto 0);
        mepc        : std_logic_vector(31 downto 0);
        irq         : std_logic;
    end record;

    type t_gprf_req is record
        read_en : std_logic;
        adr_a_i : std_logic_vector(4 downto 0);
        adr_b_i : std_logic_vector(4 downto 0);
    end record;

    type t_gprf_resp is record
        dat_a_o : std_logic_vector(31 downto 0);
        dat_b_o : std_logic_vector(31 downto 0);
    end record;

    type t_writeback is record
        data    : std_logic_vector(31 downto 0);
        reg     : std_logic_vector(4 downto 0);
        write   : std_logic;
    end record;

    type t_imem_req is record
        adr_o : std_logic_vector(31 downto 0);
        ena_o : std_logic;
    end record;

    type t_imem_resp is record
        dat_i : std_logic_vector(31 downto 0);
        ena_i : std_logic;
    end record;

    type t_dmem_req is record
        dat_o : std_logic_vector(31 downto 0);
        adr_o : std_logic_vector(31 downto 0);
        sel_o : std_logic_vector(3 downto 0);
        we_o  : std_logic;
        ena_o : std_logic;
    end record;

    type t_dmem_resp is record
        dat_i : std_logic_vector(31 downto 0);
        ena_i : std_logic;
    end record;

    ----------------------------------------------------------------------------------------------
    -- FUNCTIONS USED IN RV-LITE
    ----------------------------------------------------------------------------------------------
    function align_mem_load (data : std_logic_vector; size : t_transfer_size; address : std_logic_vector; sext : std_logic) return std_logic_vector;
    function align_mem_store (data : std_logic_vector; size : t_transfer_size) return std_logic_vector;
    function decode_mem_store (address : std_logic_vector(1 downto 0); size : t_transfer_size) return std_logic_vector;

end package;

package body core_pkg is

    -- This function aligns the memory load operation (Little endian) 
    function align_mem_load (data : std_logic_vector; size : t_transfer_size; address : std_logic_vector; sext : std_logic )
        return std_logic_vector is
        variable b      : std_logic_vector(7 downto 0) := X"00";
        variable h      : std_logic_vector(15 downto 0) := X"0000";
        variable sign   : std_logic;
        variable result : std_logic_vector(31 downto 0);
    begin
        case size is
            when BYTE => 
                case address(1 downto 0) is
                    when "00"   => b := data( 7 downto  0);
                    when "01"   => b := data(15 downto  8);
                    when "10"   => b := data(23 downto 16);
                    when "11"   => b := data(31 downto 24);
                    when others => null;
                end case;
                sign := sext and b(7);
                result(31 downto 8) := (others => sign);
                result(7 downto 0) := b;

            when HALFWORD => 
                case address(1 downto 0) is
                    when "00"   => h := data(15 downto  0);
                    when "10"   => h := data(31 downto 16);
                    when others => null;
                end case;
                sign := sext and h(15);
                result(31 downto 16) := (others => sign);
                result(15 downto 0) := h;

            when others =>
                result := data;
        end case;
        return result;
    end align_mem_load;

    -- This function repeats the operand to all positions in a memory store operation.
    function align_mem_store (data : std_logic_vector; size : t_transfer_size) return std_logic_vector is
    begin
        case size is
            when BYTE     => return data( 7 downto 0) & data( 7 downto 0) & data(7 downto 0) & data(7 downto 0);
            when HALFWORD => return data(15 downto 0) & data(15 downto 0);
            when others   => return data;
        end case;
    end align_mem_store;

    -- This function selects the correct bytes for memory writes (Little endian).
    function decode_mem_store (address : std_logic_vector(1 downto 0); size : t_transfer_size) return std_logic_vector is
    begin
        case size is
            when BYTE =>
                case address is
                    when "00"   => return "0001";
                    when "01"   => return "0010";
                    when "10"   => return "0100";
                    when "11"   => return "1000";
                    when others => return "0000";
                end case;
            when HALFWORD =>
                case address is
                    when "00"   => return "0011";
                    when "10"   => return "1100";
                    when others => return "0000";
                end case;
            when others =>
                return "1111";
        end case;
    end decode_mem_store;

end package body;
