-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : SRAM model
-------------------------------------------------------------------------------
-- File       : sram_model_8.vhd
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This simple SRAM model uses the flat memory model package.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.tl_flat_memory_model_pkg.all;

entity sram_model_8 is
generic (
    g_given_name : string;
    g_depth      : positive := 18;
    g_tAC        : time := 50 ns );
port (
    A    : in    std_logic_vector(g_depth-1 downto 0);
    DQ   : inout std_logic_vector(7 downto 0);
    
    CSn  : in    std_logic;
    OEn  : in    std_logic;
    WEn  : in    std_logic );
end sram_model_8;

architecture bfm of sram_model_8 is
    shared variable this : h_mem_object;
    signal bound         : boolean := false;
begin
    bind: process
    begin
        register_mem_model(sram_model_8'path_name, g_given_name, this);
        bound <= true;
        wait;
    end process;

    process(bound, A, CSn, OEn, WEn)
        variable addr : std_logic_vector(31 downto 0) := (others => '0');
    begin
        if bound then
            if CSn='1' then
                DQ <= (others => 'Z') after 5 ns;
            else
                addr(g_depth-1 downto 0) := A;
                if OEn = '0' then
                    DQ <= read_memory_8(this, addr) after g_tAC;
                else
                    DQ <= (others => 'Z') after 5 ns;
                end if;
                if WEn'event and WEn='1' then
                    write_memory_8(this, addr, DQ);
                end if;
            end if;
        end if;
    end process;
end bfm;
