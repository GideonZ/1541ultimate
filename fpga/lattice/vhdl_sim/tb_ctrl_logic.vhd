--------------------------------------------------------------------------------
-- Entity: tb_mem_io
-- Date:2022-03-26  
-- Author: gideon     
--
-- Description: Tries to run the controller in simulation
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity tb_ctrl_logic is
end entity;

architecture arch of tb_ctrl_logic is
    
    signal reset              : std_logic;
    signal clock              : std_logic := '0';

    signal addr_first         : std_logic_vector(21 downto 0);
    signal addr_second        : std_logic_vector(21 downto 0);
    signal csn                : std_logic_vector(1 downto 0);
    signal wdata              : std_logic_vector(31 downto 0);
    signal wdata_t            : std_logic_vector(1 downto 0);
    signal wdata_m            : std_logic_vector(3 downto 0);
    signal dqs_o              : std_logic_vector(3 downto 0);
    signal dqs_t              : std_logic_vector(1 downto 0);
    signal read               : std_logic_vector(1 downto 0);
    signal rdata              : std_logic_vector(31 downto 0);
    signal rdata_valid        : std_logic := '0';
    signal count              : unsigned(7 downto 0) := (others => '0');

    signal req                : t_mem_req_32 := c_mem_req_32_init;
    signal resp               : t_mem_resp_32;
begin
    clock <= not clock after 5 ns; -- 10 ns clock = 100 MHz    
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.ddr2_ctrl_logic
    port map(
        clock             => clock,
        reset             => reset,
        enable_sdram      => '1',
        refresh_en        => '1',
        inhibit           => '0',
        is_idle           => open,
        req               => req,
        resp              => resp,
        addr_first        => addr_first,
        addr_second       => addr_second,
        csn               => csn,
        wdata             => wdata,
        wdata_t           => wdata_t,
        wdata_m           => wdata_m,
        dqs_o             => dqs_o,
        dqs_t             => dqs_t,
        read              => read,
        rdata             => rdata,
        rdata_valid       => rdata_valid
    );
        
    process(clock)
    begin
        if rising_edge(clock) then
            if reset='0' then
                req.request <= '1';
                if resp.rack = '1' then
                    req.address <= req.address + 4;
                    req.read_writen <= not req.read_writen;
                end if;
            end if;
        end if;
    end process;

end architecture;
