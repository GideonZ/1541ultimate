
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;

entity core is
generic (
    g_hart_id   : natural := 0;
    g_start_addr: std_logic_vector(31 downto 0) := X"00000000";
    g_version   : std_logic_vector(31 downto 0) := X"475A0001"
);
port (
    imem_o      : out imem_out_type;
    imem_i      : in  imem_in_type;
    dmem_o      : out dmem_out_type;
    dmem_i      : in  dmem_in_type;
    int_i       : in  std_logic;
    rst_i       : in  std_logic;
    clk_i       : in  std_logic
);
end entity;

architecture gideon of core is
    signal dec_ready    : std_logic;
    signal ex_ready     : std_logic;
    signal wb_ready     : std_logic;

    signal fetch_i      : t_fetch_in;
    signal fetch_o      : t_fetch_out;
    signal decode_o     : t_decoded_instruction;
    signal exec_o       : t_execute_out;
    signal wb           : t_writeback;
    signal to_gprf      : t_gprf_in;
    signal from_gprf    : t_gprf_out;
    signal to_csr       : t_csr_in;
    signal from_csr     : t_csr_out;

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
    reg_rd_d     <= exec_o.reg_rd when rising_edge(clk_i);
    reg_write_d  <= exec_o.reg_write when rising_edge(clk_i);
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
        fetch_o => fetch_o,
        imem_o  => imem_o,
        fetch_i => fetch_i,
        imem_i  => imem_i,
        rst_i   => rst_i,
        rdy_i   => dec_ready,
        clk_i   => clk_i
    );

    i_decode: entity work.decode
    port map (
        flush    => exec_o.do_jump,
        hazard   => hazard,
        decode_o => decode_o,
        gprf_o   => to_gprf,
        decode_i => fetch_o,
        irq_i    => from_csr.irq,
        rdy_i    => ex_ready,
        rdy_o    => dec_ready,
        rst_i    => rst_i,
        clk_i    => clk_i
    );

    i_gprf: entity work.gprf
    port map (
        gprf_o => from_gprf,
        gprf_i => to_gprf,
        wb_i   => wb,
        clk_i  => clk_i
    );

    i_exec: entity work.execute
      port map (
        flush  => exec_o.do_jump,
        exec_i => decode_o,
        gprf_i => from_gprf,
        exec_o => exec_o,
        dmem_o => dmem_o,
        csr_i  => from_csr,
        csr_o  => to_csr,

        rdy_i  => wb_ready,
        rdy_o  => ex_ready,
        rst_i  => rst_i,
        clk_i  => clk_i
      );


    i_csr: entity work.csr
    generic map (
        g_hart_id => g_hart_id,
        g_version => g_version
    )
    port map (
        int_i => int_i,
        csr_i => to_csr,
        csr_o => from_csr,
        ena_i => ex_ready,
        rst_i => rst_i,
        clk_i => clk_i
    );

    i_wb: entity work.writeback
    port map (
        exec_i => exec_o,
        rdy_o  => wb_ready,
        dmem_i => dmem_i,
        wb_o   => wb
    );
    
end architecture;
