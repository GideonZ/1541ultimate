----------------------------------------------------------------------------------------------
--
--      Input file         : gprf.vhd
--      Design name        : gprf
--      Author             : Tamar Kranenburg
--      Company            : Delft University of Technology
--                         : Faculty EEMCS, Department ME&CE
--                         : Systems and Circuits group
--
--      Description        : The general purpose register infers memory blocks to implement
--                           the register file. All outputs are registered, possibly by using
--                           registered memory elements.
--
----------------------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;

entity gprf is port
(
    gprf_o : out t_gprf_out;
    gprf_i : in t_gprf_in;
    wb_i   : in t_writeback;
    ena_i  : in std_logic;
    clk_i  : in std_logic
);
end entity;

-- This architecture is the default implementation. It
-- consists of two dual port memories. Other
-- architectures can be added while configurations can
-- control the implemented architecture.
architecture arch of gprf is
begin
    
    a : entity work.dsram
    generic map
    (
        WIDTH => 32,
        SIZE  => 5
    )
    port map
    (
        dat_o   => gprf_o.dat_a_o,
        adr_i   => gprf_i.adr_a_i,
        ena_i   => ena_i,
        dat_w_i => wb_i.data,
        adr_w_i => wb_i.reg,
        wre_i   => wb_i.write,
        clk_i   => clk_i
    );

    b : entity work.dsram
    generic map
    (
        WIDTH => 32,
        SIZE  => 5
    )
    port map
    (
        dat_o   => gprf_o.dat_b_o,
        adr_i   => gprf_i.adr_b_i,
        ena_i   => ena_i,
        dat_w_i => wb_i.data,
        adr_w_i => wb_i.reg,
        wre_i   => wb_i.write,
        clk_i   => clk_i
    );

end architecture;
