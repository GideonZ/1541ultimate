
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.tl_flat_memory_model_pkg.all;


entity mem_bus_32_slave_bfm is
generic (
    g_name      : string;
    g_time_to_ack : natural := 0;
    g_latency	: positive := 2 );
port (
    clock       : in    std_logic;

    req         : in    t_mem_req_32;
    resp        : out   t_mem_resp_32 );

end mem_bus_32_slave_bfm;

architecture bfm of mem_bus_32_slave_bfm is
    shared variable mem : h_mem_object;
	signal bound		: boolean := false;
    signal delay        : t_mem_req_32_array(0 to g_time_to_ack) := (others => c_mem_req_32_init);
	signal req_i        : t_mem_req_32 := c_mem_req_32_init;
	signal pipe			: t_mem_req_32_array(0 to g_latency-1) := (others => c_mem_req_32_init);
    signal suppress     : std_logic := '0';
begin
    -- this process registers this instance of the bfm to the server package
    bind: process
    begin
        register_mem_model(mem_bus_32_slave_bfm'path_name, g_name, mem);
        bound <= true;
        wait;
    end process;

    -- Time to ack is implemented with a delay line on the requestor
    process(req, suppress)
    begin
        delay(0) <= req;
        delay(0).request <= req.request and not suppress;    
    end process;
    req_i <= delay(g_time_to_ack);
    
	resp.rack     <= '1' when bound and req_i.request='1' else '0';
	resp.rack_tag <= req_i.tag when bound and req_i.request='1' else (others => '0');

    process(clock)
        variable data : std_logic_vector(31 downto 0);
        variable word_addr : unsigned(31 downto 2);
        variable byte_addr : unsigned(1 downto 0);
    begin
        if rising_edge(clock) then
            delay(1 to g_time_to_ack) <= delay(0 to g_time_to_ack-1);
            if req.request = '1' then
                suppress <= '1';
            end if;
            if req_i.request = '1' then
                suppress <= '0';
            end if;

			pipe(0 to g_latency-2) <= pipe(1 to g_latency-1);
			pipe(g_latency-1) <= req_i;
			resp.dack_tag <= (others => '0');
			resp.data     <= (others => '0');

            if bound then
				if pipe(0).request='1' then
					if pipe(0).read_writen='1' then
						resp.dack_tag <= pipe(0).tag;
						data := read_memory_32(mem, "000000" & std_logic_vector(pipe(0).address));
                        if pipe(0).address(1 downto 0) = "00" then
                            resp.data     <= data;
                        elsif pipe(0).address(1 downto 0) = "01" then
                            resp.data     <= data(7 downto 0) & data(31 downto 8);
                        elsif pipe(0).address(1 downto 0) = "10" then
                            resp.data     <= data(15 downto 0) & data(31 downto 16);
                        else
                            resp.data     <= data(23 downto 0) & data(31 downto 24);
                        end if;
                    else
                        word_addr := "000000" & pipe(0).address(25 downto 2);
                        byte_addr := pipe(0).address(1 downto 0);
                        for i in 0 to 3 loop
                            if pipe(0).byte_en(i) = '1' then
                                write_memory_8(mem, std_logic_vector(word_addr) & std_logic_vector(byte_addr), pipe(0).data(7+8*i downto 8*i));
                            end if;
                            byte_addr := byte_addr + 1;
                        end loop;
					end if;
				end if;
			end if; 
		end if; 
    end process;

end bfm;
