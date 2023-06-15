--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: A simple dual-port synchronous RAM; synthesizable / inferable in
--              Xilinx, Lattice and Altera. It has one read port and one write
--              port, both on the same clock. This block is used for the
--              register file, and (outside of the core) for the I-cache.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dsram is
generic
(
    g_ram_style : string := "auto";
    WIDTH : positive := 32;
    SIZE  : positive := 8
);
port
(
    dat_o   : out std_logic_vector(WIDTH - 1 downto 0);
    adr_i   : in std_logic_vector(SIZE - 1 downto 0);
    ena_i   : in std_logic;
    dat_w_i : in std_logic_vector(WIDTH - 1 downto 0);
    adr_w_i : in std_logic_vector(SIZE - 1 downto 0);
    wre_i   : in std_logic;
    clk_i   : in std_logic
);
end entity;

architecture arch of dsram is
    type ram_type is array(0 to 2 ** SIZE - 1) of std_logic_vector(WIDTH - 1 downto 0);
    signal ram :  ram_type := (others => (others => '0'));
    attribute ram_style : string;
    attribute ram_style of ram : signal is g_ram_style;
    attribute syn_ramstyle : string;
    attribute syn_ramstyle of ram : signal is g_ram_style;
begin
    process(clk_i)
    begin
        if rising_edge(clk_i) then
            if ena_i = '1' then
                dat_o <= ram(to_integer(unsigned(adr_i)));
            end if;
            if wre_i = '1' then
                ram(to_integer(unsigned(adr_w_i))) <= dat_w_i;
            end if;
        end if;
    end process;
end architecture;
