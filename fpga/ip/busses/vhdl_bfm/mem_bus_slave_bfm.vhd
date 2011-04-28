
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.tl_flat_memory_model_pkg.all;


entity mem_bus_slave_bfm is
generic (
    g_name      : string;
    g_latency	: positive := 2 );
port (
    clock       : in    std_logic;

    req         : in    t_mem_req;
    resp        : out   t_mem_resp );

end mem_bus_slave_bfm;

architecture bfm of mem_bus_slave_bfm is
    shared variable mem : h_mem_object;
	signal bound		: boolean := false;
	signal pipe			: t_mem_req_array(0 to g_latency-1) := (others => c_mem_req_init);
begin
    -- this process registers this instance of the bfm to the server package
    bind: process
    begin
        register_mem_model(mem_bus_slave_bfm'path_name, g_name, mem);
        bound <= true;
        wait;
    end process;

	resp.rack     <= '1' when bound and req.request='1' else '0';
	resp.rack_tag <= req.tag when bound and req.request='1' else (others => '0');

    process(clock)
    begin
        if rising_edge(clock) then
			pipe(0 to g_latency-2) <= pipe(1 to g_latency-1);
			pipe(g_latency-1) <= req;
			resp.dack_tag <= (others => '0');
			resp.data     <= (others => '0');

            if bound then
				if pipe(0).request='1' then
					if pipe(0).read_writen='1' then
						resp.dack_tag <= pipe(0).tag;
						resp.data     <= read_memory_8(mem, "000000" & std_logic_vector(pipe(0).address));
					else
						write_memory_8(mem, "000000" & std_logic_vector(pipe(0).address), pipe(0).data);
					end if;
				end if;
			end if; 
		end if; 
    end process;

end bfm;
