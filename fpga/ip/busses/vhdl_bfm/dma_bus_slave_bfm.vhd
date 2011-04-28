
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.dma_bus_pkg.all;
use work.tl_flat_memory_model_pkg.all;


entity dma_bus_slave_bfm is
generic (
    g_name      : string;
    g_latency	: positive := 2 );
port (
    clock       : in    std_logic;

    req         : in    t_dma_req;
    resp        : out   t_dma_resp );

end dma_bus_slave_bfm;

architecture bfm of dma_bus_slave_bfm is
    shared variable mem : h_mem_object;
	signal bound		: boolean := false;
	signal pipe			: t_dma_req_array(0 to g_latency-1) := (others => c_dma_req_init);
	signal resp_i		: t_dma_resp;
begin
    -- this process registers this instance of the bfm to the server package
    bind: process
    begin
        register_mem_model(dma_bus_slave_bfm'path_name, g_name, mem);
        bound <= true;
        wait;
    end process;

	resp <= resp_i;
	
    process(clock)
    begin
        if rising_edge(clock) then
			pipe(0 to g_latency-2) <= pipe(1 to g_latency-1);
			pipe(g_latency-1) <= req;
			if resp_i.rack='1' then
				pipe(g_latency-1).request <= '0';
			end if;
			resp_i.data <= (others => '0');
			resp_i.dack <= '0';
			resp_i.rack <= '0';

            if bound then
				resp_i.rack <= req.request and not resp_i.rack;
				if pipe(0).request='1' then
					if pipe(0).read_writen='1' then
						resp_i.data     <= read_memory_8(mem, X"0000" & std_logic_vector(pipe(0).address));
						resp_i.dack     <= '1';
					else
						write_memory_8(mem, X"0000" & std_logic_vector(pipe(0).address), pipe(0).data);
					end if;
				end if;
			end if; 
		end if; 
    end process;

end bfm;
