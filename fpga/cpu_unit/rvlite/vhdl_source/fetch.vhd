----------------------------------------------------------------------------------------------
--
--      Input file         : fetch.vhd
--      Design name        : fetch
--      Author             : Tamar Kranenburg
--      Company            : Delft University of Technology
--                         : Faculty EEMCS, Department ME&CE
--                         : Systems and Circuits group
--
--      Description        : Instruction Fetch Stage inserts instruction into the pipeline. It
--                           uses a single port Random Access Memory component which holds
--                           the instructions. The next instruction is computed in the decode
--                           stage.
--
----------------------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;

entity fetch is
generic (
    g_start_addr    : std_logic_vector(31 downto 0) := X"00000000"
);
port
(
    fetch_o : out t_fetch_out;
    fetch_i : in  t_fetch_in;
    imem_o  : out imem_out_type;
    imem_i  : in  imem_in_type;
    rst_i   : in std_logic;
    rdy_i   : in std_logic;
    clk_i   : in std_logic
);
end fetch;

architecture arch of fetch is
    signal pc, pc_next    : std_logic_vector(31 downto 0);
    signal rst_d          : std_logic;
    signal ena_o          : std_logic;
    signal possibly_valid : std_logic;

begin
    fetch_o.program_counter <= pc;
    fetch_o.instruction <= imem_i.dat_i;
    fetch_o.inst_valid <= possibly_valid and imem_i.ena_i;
    
    ena_o <= rdy_i and not rst_d;
    imem_o.adr_o <= pc_next;
    imem_o.ena_o <= ena_o;

    fetch_comb: process(fetch_i, imem_i, pc, rdy_i, possibly_valid)
    begin
        pc_next <= pc;
        if fetch_i.branch = '1' then
            pc_next <= fetch_i.branch_target;
            pc_next(1 downto 0) <= "00";
        elsif imem_i.ena_i = '1' and possibly_valid = '1' then
            pc_next <= std_logic_vector(unsigned(pc) + 4);
        end if;
    end process;

    fetch_seq: process(clk_i)
    begin
        if rising_edge(clk_i) then
            rst_d <= rst_i;
            if rst_d = '1' then
                pc <= g_start_addr;
                possibly_valid <= '0';
            else
                if rdy_i = '1' and imem_i.ena_i = '1' then
                    possibly_valid <= '0';
                end if;
                if (imem_i.ena_i = '1' and ena_o = '1') or fetch_i.branch = '1' then
                    pc <= pc_next;
                    possibly_valid <= imem_i.ena_i and ena_o;
                end if;
            end if;
        end if;
    end process;

end arch;