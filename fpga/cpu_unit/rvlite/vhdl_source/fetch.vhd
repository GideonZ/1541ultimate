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
    ena_i   : in std_logic;
    clk_i   : in std_logic
);
end fetch;

architecture arch of fetch is
    signal r, rin   : t_fetch_out;
    signal rst_d    : std_logic;
    signal ena_o    : std_logic;
    signal possibly_valid   : std_logic;
begin

    fetch_o.program_counter <= r.program_counter;
    fetch_o.instruction <= imem_i.dat_i;
    fetch_o.inst_valid <= possibly_valid and imem_i.ena_i;
    
    ena_o <= ena_i and imem_i.ena_i and not rst_i;
    imem_o.adr_o <= rin.program_counter;
    imem_o.ena_o <= ena_o;

    fetch_comb: process(fetch_i, imem_i, r, rst_d)
        variable v : t_fetch_out;
    begin
        v := r;
        if rst_d = '1' then
            v.program_counter := g_start_addr;
        elsif fetch_i.branch = '1' then
            v.program_counter := fetch_i.branch_target;
        elsif fetch_i.hazard = '1' or imem_i.ena_i = '0' then
            v.program_counter := r.program_counter;
        else
            v.program_counter := std_logic_vector(unsigned(r.program_counter) + 4);
        end if;
        rin <= v;
    end process;

    fetch_seq: process(clk_i)
    begin
        if rising_edge(clk_i) then
            if rst_i = '1' then
                r.program_counter <= g_start_addr;
                rst_d <= '1';
                possibly_valid <= '0';
            elsif ena_i = '1' then
                r <= rin;
                rst_d <= '0';
                if imem_i.ena_i = '1' then
                    possibly_valid <= ena_o;
                end if;
            end if;
        end if;
    end process;

end arch;