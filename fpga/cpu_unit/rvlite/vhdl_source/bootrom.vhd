
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.mem_bus_pkg.all;
use work.bootrom_pkg.all;

entity bootrom is
port
(
    clock       : in  std_logic;
    reset       : in  std_logic;
    ireq        : in  t_bram_req;
    iresp       : out t_bram_resp;
    dreq        : in  t_bram_req;
    dresp       : out t_bram_resp
);
end entity;

architecture arch of bootrom is
    constant g_width_bits   : positive := 32;
    constant g_depth_bits   : positive := 10; -- 4K
    constant g_read_first_a : boolean := false;
    constant g_read_first_b : boolean := false;

    shared variable ram : t_boot_data(0 to 2**g_depth_bits-1) := c_bootrom;

    -- Xilinx and Altera attributes
    attribute ram_style        : string;
    attribute ram_style of ram : variable is "auto";
    -- Altera Attribute
    attribute ramstyle         : string;
    attribute ramstyle of ram  : variable is "auto";
    -- Lattice Attribute
    attribute syn_ramstyle     : string;
    attribute syn_ramstyle of ram : variable is "auto";

begin
    -----------------------------------------------------------------------
    -- PORT A
    -----------------------------------------------------------------------
    p_port_a: process(clock)
    begin
        if rising_edge(clock) then
            if ireq.ena = '1' then
                if g_read_first_a then
                    iresp.data <= ram(to_integer(ireq.address(g_depth_bits+1 downto 2)));
                end if;
                
                if ireq.wen = '1' then
                    ram(to_integer(ireq.address(g_depth_bits+1 downto 2))) := ireq.data;
                end if;

                if not g_read_first_a then
                    iresp.data <= ram(to_integer(ireq.address(g_depth_bits+1 downto 2)));
                end if;
            end if;
        end if;
    end process;

    -----------------------------------------------------------------------
    -- PORT B
    -----------------------------------------------------------------------
    p_port_b: process(clock)
    begin
        if rising_edge(clock) then
            if dreq.ena = '1' then
                if g_read_first_b then
                    dresp.data <= ram(to_integer(dreq.address(g_depth_bits+1 downto 2)));
                end if;

                if dreq.wen = '1' then
                    ram(to_integer(dreq.address(g_depth_bits+1 downto 2))) := dreq.data;
                end if;

                if not g_read_first_b then
                    dresp.data <= ram(to_integer(dreq.address(g_depth_bits+1 downto 2)));
                end if;
            end if;
        end if;
    end process;
end architecture;
