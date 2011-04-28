
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.dma_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.slot_bus_pkg.all;
--use work.slot_bus_master_bfm_pkg.all;

entity harness_reu is

end harness_reu;

architecture harness of harness_reu is
	signal clock		: std_logic := '0';
    signal reset        : std_logic;
    signal slot_req     : t_slot_req;
    signal slot_resp    : t_slot_resp;
    signal mem_req      : t_mem_req;
    signal mem_resp     : t_mem_resp;
    signal dma_req      : t_dma_req;
    signal dma_resp     : t_dma_resp;
    signal phi2_tick    : std_logic := '0';
    signal reu_dma_n    : std_logic := '1';
begin
	clock <= not clock after 10 ns;
	reset <= '1', '0' after 100 ns;
	
	i_reu: entity work.reu
	generic map (
	    g_ram_tag       => X"10",
	    g_extended      => false,
	    g_ram_base      => X"0000000" )
	port map (
	    clock           => clock,
	    reset           => reset,
	    
	    -- register interface
	    slot_req        => slot_req,
	    slot_resp       => slot_resp,
	    
	    -- system interface
	    phi2_tick       => phi2_tick,
	    reu_dma_n       => reu_dma_n,
	    size_ctrl       => "111",
	    enable          => '1',
	    
	    -- memory interface
	    mem_req         => mem_req,
	    mem_resp        => mem_resp,
	    
	    dma_req         => dma_req,
	    dma_resp        => dma_resp );

	
	i_slot_master: entity work.slot_bus_master_bfm
	generic map (
	    g_name      => "slot master" )
	port map (
	    clock       => clock,
	
	    req         => slot_req,
	    resp        => slot_resp );
		
	i_c64_memory: entity work.dma_bus_slave_bfm
	generic map (
	    g_name      => "c64_memory",
	    g_latency	=> 4 )
	port map (
	    clock       => clock,
	
	    req         => dma_req,
	    resp        => dma_resp );

	i_reu_memory: entity work.mem_bus_slave_bfm
	generic map (
	    g_name      => "reu_memory",
	    g_latency	=> 2 )
	port map (
	    clock       => clock,
	
	    req         => mem_req,
	    resp        => mem_resp );

end harness;
