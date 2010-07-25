-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : BRAM model
-------------------------------------------------------------------------------
-- File       : bram_model_8sp.vhd
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This simple BRAM model uses the flat memory model package.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.tl_flat_memory_model_pkg.all;

entity bram_model_8sp is
generic (
    g_given_name : string;
    g_depth      : positive := 18 );
port (
	CLK  : in  std_logic;
	SSR  : in  std_logic;
	EN   : in  std_logic;
	WE   : in  std_logic;
    ADDR : in  std_logic_vector(g_depth-1 downto 0);
	DI   : in  std_logic_vector(7 downto 0);
	DO   : out std_logic_vector(7 downto 0) );
end bram_model_8sp;

architecture bfm of bram_model_8sp is
    shared variable this : h_mem_object;
    signal bound         : boolean := false;
begin
    bind: process
    begin
        register_mem_model(bram_model_8sp'path_name, g_given_name, this);
        bound <= true;
        wait;
    end process;

    process(CLK)
        variable vaddr : std_logic_vector(31 downto 0) := (others => '0');
    begin
        if rising_edge(CLK) then
            vaddr(g_depth-1 downto 0) := ADDR;

            if EN='1' then
                if bound then
                    DO <= read_memory_8(this, vaddr);
                    if WE='1' then
                        write_memory_8(this, vaddr, DI);
                    end if;
                end if;
            end if;
            if SSR='1' then
                DO <= (others => '0');
            end if;
        end if;
    end process;
end bfm;
