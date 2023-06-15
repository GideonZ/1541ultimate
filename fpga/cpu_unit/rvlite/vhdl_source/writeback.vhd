--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: The writeback module simply implements a multiplexer for the
--              data that is to be written to the register file. The mux
--              setting comes from exec, just like the output of the ALU and
--              CSR. The data from the memory comes directly from the data
--              port of the processor.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;

entity writeback is
port
(
    exec_i      : in  t_execute_out;
    dmem_i      : in  t_dmem_resp;
    rdy_o       : out std_logic;
    wb_o        : out t_writeback
);
end entity;

architecture arch of writeback is
    signal mem_data     : std_logic_vector(31 downto 0);
begin
    mem_data <= align_mem_load(dmem_i.dat_i, exec_i.mem_transfer_size, exec_i.mem_offset, exec_i.mem_read_sext);
    with exec_i.reg_write_sel select wb_o.data <=
        exec_i.alu_result  when WB_ALU,
        exec_i.csr_data    when WB_CSR,
        mem_data           when WB_MEM,
        exec_i.pc_plus4    when WB_PC4,
        exec_i.rel_address when WB_REL,
        X"00000000" when others;

    wb_o.reg     <= exec_i.reg_rd;
    wb_o.write   <= exec_i.reg_write;

    rdy_o <= dmem_i.ena_i;
end architecture;
