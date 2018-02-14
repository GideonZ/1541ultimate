
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity mem_bus_32_slave_bfm is
generic (
    g_latency	: positive := 2 );
port (
    clock       : in    std_logic;

    req         : in    t_mem_req_32;
    resp        : out   t_mem_resp_32 );

end entity;

architecture bfm of mem_bus_32_slave_bfm is
	signal pipe			: t_mem_req_32_array(0 to g_latency-1) := (others => c_mem_req_32_init);
    type t_word_array is array(natural range <>) of std_logic_vector(31 downto 0);
    shared variable mem_array : t_word_array(0 to 16#3FFFFF#) := (others => X"33333333"); -- we need 16 megs
begin

	resp.rack     <= '1' when req.request='1' else '0';
	resp.rack_tag <= req.tag when req.request='1' else (others => '0');

    process(clock)
        variable rdata  : std_logic_vector(31 downto 0);
    begin
        if rising_edge(clock) then
			pipe(0 to g_latency-2) <= pipe(1 to g_latency-1);
			pipe(g_latency-1) <= req;
			resp.dack_tag <= (others => '0');
			resp.data     <= (others => '0');

			if pipe(0).request='1' then
				if pipe(0).read_writen='1' then
					resp.dack_tag <= pipe(0).tag;
					resp.data     <= mem_array(to_integer(pipe(0).address(23 downto 2)));
				else
                    if pipe(0).byte_en = "1111" then
                        mem_array(to_integer(pipe(0).address(23 downto 2))) := pipe(0).data;
                    else
                        rdata := mem_array(to_integer(pipe(0).address(23 downto 2)));
                        for i in 0 to 3 loop
                            if pipe(0).byte_en(i) = '1' then
                                rdata(8*i+7 downto 8*i) := pipe(0).data(8*i+7 downto 8*i);
                            end if;
                        end loop;
                        mem_array(to_integer(pipe(0).address(23 downto 2))) := rdata;
                    end if;
				end if;
			end if;
		end if; 
    end process;

end bfm;
