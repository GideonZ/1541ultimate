--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: The 'gprf' implements the General Purpose Register File, with
--              two read ports and one write port. This is implemented by
--              two RAMs in parallel that are both written with the same data.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use work.core_pkg.all;

entity gprf is
generic (
    g_ram_style : string := "distributed"
);
port (
    gprf_o : out t_gprf_resp;
    gprf_i : in t_gprf_req;
    wb_i   : in t_writeback;
    clock  : in std_logic
);
end entity;


architecture arch of gprf is
begin
    
    a : entity work.dsram
    generic map
    (
        g_ram_style => g_ram_style,
        g_width => 32,
        g_depth => 5
    )
    port map
    (
        dat_o   => gprf_o.dat_a_o,
        adr_i   => gprf_i.adr_a_i,
        ena_i   => gprf_i.read_en,
        dat_w_i => wb_i.data,
        adr_w_i => wb_i.reg,
        wre_i   => wb_i.write,
        clock   => clock
    );

    b : entity work.dsram
    generic map
    (
        g_ram_style => g_ram_style,
        g_width => 32,
        g_depth => 5
    )
    port map
    (
        dat_o   => gprf_o.dat_b_o,
        adr_i   => gprf_i.adr_b_i,
        ena_i   => gprf_i.read_en,
        dat_w_i => wb_i.data,
        adr_w_i => wb_i.reg,
        wre_i   => wb_i.write,
        clock   => clock
    );

end architecture;
