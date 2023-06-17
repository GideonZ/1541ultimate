--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: Top level of the Risc-V CPU core, dubbed "Frenix".
--              The CPU has two buses; the instruction bus and data bus. Both
--              buses assume a one-cycle latency from the request to the
--              response. When wait cycles are necessary, the 'ena_i' signal
--              can be de-asserted in the response. The interrupt input is
--              level triggered, and masked through the CSR. It causes the
--              external machine interupt (MIE), cause 0x8000000B.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use work.core_pkg.all;

entity core is
generic (
    g_gprf_ram  : string := "auto";
    g_hart_id   : natural := 0;
    g_start_addr: std_logic_vector(31 downto 0) := X"00000000";
    g_version   : std_logic_vector(31 downto 0) := X"475A0001"
);
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    irq         : in  std_logic;
    imem_req    : out t_imem_req;
    imem_resp   : in  t_imem_resp;
    dmem_req    : out t_dmem_req;
    dmem_resp   : in  t_dmem_resp
);
end entity;

architecture structural of core is
    signal dec_ready    : std_logic;
    signal ex_ready     : std_logic;
    signal wb_ready     : std_logic;

    signal fetch_i      : t_fetch_in;
    signal fetch_o      : t_fetch_out;
    signal decode_o     : t_decode_out;
    signal exec_o       : t_execute_out;
    signal wb           : t_writeback;
    signal gprf_req     : t_gprf_req;
    signal gprf_resp    : t_gprf_resp;
    signal csr_req      : t_csr_req;
    signal csr_resp     : t_csr_resp;

    signal hazard_rs1ex : std_logic;
    signal hazard_rs2ex : std_logic;
    signal hazard_rs1wb : std_logic;
    signal hazard_rs2wb : std_logic;
    signal hazard       : std_logic;
    signal reg_rd_d     : std_logic_vector(4 downto 0);
    signal reg_write_d  : std_logic;
begin
    fetch_i.branch <= exec_o.do_jump;
    fetch_i.branch_target <= exec_o.target_pc;

    -- Hazards exists when read from register file is from data that still needs to be written to regfile.
    -- In case of this pipeline, the invalid register data coincides with 'decode_o', or 'exec_i'.
    -- 'decode_o' says: "I have read the register", and the hazard detection says: "fine, but it was invalid,
    -- read it again!"
    -- This means that the decoded instruction becomes invalid and exec should not execute it.
    reg_rd_d     <= exec_o.reg_rd when rising_edge(clock);
    reg_write_d  <= exec_o.reg_write when rising_edge(clock);
    hazard_rs1ex <= decode_o.reg_rs1_read when (exec_o.reg_rd = decode_o.reg_rs1 and exec_o.reg_write = '1') else '0';
    hazard_rs2ex <= decode_o.reg_rs2_read when (exec_o.reg_rd = decode_o.reg_rs2 and exec_o.reg_write = '1') else '0';
    hazard_rs1wb <= decode_o.reg_rs1_read when (reg_rd_d = decode_o.reg_rs1 and reg_write_d = '1') else '0';
    hazard_rs2wb <= decode_o.reg_rs2_read when (reg_rd_d = decode_o.reg_rs2 and reg_write_d = '1') else '0';
    hazard       <= hazard_rs1ex or hazard_rs2ex or hazard_rs1wb or hazard_rs2wb;

    i_fetch: entity work.fetch
    generic map (
        g_start_addr => g_start_addr
    )
    port map (
        clock   => clock,
        reset   => reset,
        fetch_i => fetch_i,
        fetch_o => fetch_o,
        imem_o  => imem_req,
        imem_i  => imem_resp,
        rdy_i   => dec_ready
    );

    i_decode: entity work.decode
    port map (
        clock    => clock,
        reset    => reset,
        decode_i => fetch_o,
        decode_o => decode_o,
        gprf_o   => gprf_req,
        irq_i    => csr_resp.irq,
        flush    => exec_o.do_jump,
        hazard   => hazard,
        rdy_i    => ex_ready,
        rdy_o    => dec_ready
    );

    i_gprf: entity work.gprf
    generic map (
        g_ram_style => g_gprf_ram
    )
    port map (
        clock  => clock,
        gprf_i => gprf_req,
        gprf_o => gprf_resp,
        wb_i   => wb
    );

    i_exec: entity work.execute
    port map (
        clock     => clock,
        reset     => reset,
        exec_i    => decode_o,
        exec_o    => exec_o,
        gprf_i    => gprf_resp,
        csr_resp  => csr_resp,
        csr_req   => csr_req,
        dmem_o    => dmem_req,
        flush     => exec_o.do_jump,
        rdy_i     => wb_ready,
        rdy_o     => ex_ready
      );


    i_csr: entity work.csr
    generic map (
        g_hart_id => g_hart_id,
        g_version => g_version
    )
    port map (
        clock => clock,
        reset => reset,
        ena_i => wb_ready,
        int_i => irq,
        csr_i => csr_req,
        csr_o => csr_resp
    );

    i_wb: entity work.writeback
    port map (
        exec_i => exec_o,
        rdy_o  => wb_ready,
        dmem_i => dmem_resp,
        wb_o   => wb
    );
    
end architecture;
