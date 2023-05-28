
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;
use work.alu_pkg.all;

-- synopsis translate_off
-- synthesis translate_off
use work.tl_string_util_pkg.all;
-- synthesis translate_on
-- synopsis translate_on
library std;
use std.textio.all;

entity execute is
port
(
    flush       : in  std_logic;
    exec_i      : in  t_decoded_instruction;
    gprf_i      : in  t_gprf_out;
    exec_o      : out t_execute_out;
    dmem_o      : out dmem_out_type;
    csr_i       : in  t_csr_out;
    csr_o       : out t_csr_in;
    rdy_o       : out std_logic;
    rdy_i       : in std_logic;
    rst_i       : in std_logic;
    clk_i       : in std_logic
);
end entity;

architecture arch of execute is
    signal exec_c       : t_execute_out;
    signal exec_r       : t_execute_out;
    signal alu_data_2   : std_logic_vector(31 downto 0);
    signal branch_taken : std_logic;
    signal mem_address  : std_logic_vector(31 downto 0);
    signal rel_address  : std_logic_vector(31 downto 0);
    signal target_pc    : std_logic_vector(31 downto 0);
    signal do_jump      : std_logic;
    signal inst_valid   : std_logic;
begin
    with exec_i.alu_src_b select alu_data_2 <=
        gprf_i.dat_b_o   when ALU_SRC_REGB,
        exec_i.imm_value when ALU_SRC_IMM,
        X"00000000"      when others;

    i_alu: entity work.alu_branch
    port map (
        data_1       => gprf_i.dat_a_o,
        data_2       => alu_data_2,
        oper         => exec_i.alu_operation,
        func         => exec_i.alu_func,
        branch_taken => branch_taken,
        data_out     => exec_c.alu_result
    );

    mem_address <= std_logic_vector(unsigned(gprf_i.dat_a_o) + unsigned(exec_i.imm_value));
    rel_address <= std_logic_vector(unsigned(exec_i.program_counter) + unsigned(exec_i.imm_value));

    with exec_i.flow_ctrl select do_jump <= 
        '1' when FL_JUMP,
        branch_taken when FL_COND,
        '0' when others;

    with exec_i.flow_target select target_pc <=
        mem_address when TRGT_MEM,
        rel_address when TRGT_REL,
        csr_i.mepc  when TRGT_MEPC,
        csr_i.mtvec when TRGT_TRAP,
        X"00000000" when others;

    inst_valid <= exec_i.valid and not flush;
    
    -- Port to CSR
    csr_o.address <= exec_i.imm_value(11 downto 0);
    csr_o.wdata   <= gprf_i.dat_a_o when exec_i.csr_sel = CSR_REG else sign_extend(exec_i.reg_rs1, exec_i.reg_rs1(4), 32);
    csr_o.oper    <= exec_i.csr_action;
    csr_o.enable  <= exec_i.csr_access and inst_valid;
    csr_o.inhibit_irq <= exec_i.csr_access; -- even during a hazard
    csr_o.trap.trap   <= inst_valid when (exec_i.flow_target = TRGT_TRAP) and (exec_i.flow_ctrl = FL_JUMP) else '0';
    csr_o.trap.mret   <= inst_valid when (exec_i.flow_target = TRGT_MEPC) and (exec_i.flow_ctrl = FL_JUMP) else '0';
    csr_o.trap.cause  <= exec_i.cause;
    csr_o.trap.program_counter <= exec_i.program_counter;

    -- Port to memory (combinatorial)
    dmem_o.adr_o <= mem_address;
    dmem_o.dat_o <= align_mem_store(gprf_i.dat_b_o, exec_i.mem_transfer_size);
    dmem_o.sel_o <= decode_mem_store(mem_address(1 downto 0), exec_i.mem_transfer_size);
    dmem_o.we_o  <= exec_i.mem_write and inst_valid;
    dmem_o.ena_o <= (exec_i.mem_write or exec_i.mem_read) and inst_valid; 

    -- Work done here
    exec_c.do_jump           <= do_jump and inst_valid;
    exec_c.target_pc         <= target_pc;
    -- Pass throughs
    exec_c.program_counter   <= exec_i.program_counter;
    exec_c.mem_transfer_size <= exec_i.mem_transfer_size;
    exec_c.mem_read_sext     <= exec_i.mem_read_sext;
    exec_c.reg_rd            <= exec_i.reg_rd;
    exec_c.mem_offset        <= mem_address(1 downto 0);
    exec_c.reg_write         <= (exec_i.reg_write and inst_valid) when exec_i.reg_rd /= "00000" else '0';
    exec_c.reg_write_sel     <= exec_i.reg_write_sel;
    exec_c.csr_data          <= csr_i.rdata;
    exec_c.pc_plus4          <= std_logic_vector(unsigned(exec_i.program_counter) + 4);
    exec_c.rel_address       <= target_pc; --rel_address;
    exec_c.valid             <= exec_i.valid; --inst_valid;

    process(clk_i)
        variable s : line;
    begin
        if rising_edge(clk_i) then
            if rst_i = '1' then
                exec_r <= c_execute_nop; 
            elsif rdy_i = '1' or exec_r.valid = '0' then
                exec_r <= exec_c;
            end if;
-- synthesis translate_off
-- synopsis translate_off
            if exec_i.valid = '1' and rdy_i = '1' and false then
                write(s, "PC: " & hstr(exec_i.program_counter) & " IMM: " & hstr(exec_i.imm_value) 
                    & " RS1: " & hstr(exec_i.reg_rs1) & ":" & hstr(gprf_i.dat_a_o) 
                    & " RS2: " & hstr(exec_i.reg_rs2) & ":" & hstr(gprf_i.dat_b_o)
                    & " RD:  " & hstr(exec_i.reg_rd) & ":" & t_reg_write_sel'image(exec_i.reg_write_sel)
                    & " ALU: " & hstr(exec_c.alu_result)
                    & " MEMA:" & hstr(mem_address) & "->" & std_logic'image(exec_i.mem_write)
                );
                writeline(output, s);
            end if;
-- synthesis translate_on
-- synopsis translate_on
        end if;
    end process;

    exec_o <= exec_r;
    rdy_o <= rdy_i or not exec_r.valid;

end architecture;
