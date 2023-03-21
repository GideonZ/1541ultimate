library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity mem_bus_arbiter_pri_32 is
generic (
    g_registered: boolean := false;
    g_ports     : positive := 3 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    inhibit     : in  std_logic := '0'; -- when '1', the output request is squelched
    
    reqs        : in  t_mem_req_32_array(0 to g_ports-1);
    resps       : out t_mem_resp_32_array(0 to g_ports-1);
    
    req         : out t_mem_req_32;
    resp        : in  t_mem_resp_32 );
end entity;

architecture rtl of mem_bus_arbiter_pri_32 is
    signal req_i    : t_mem_req_32;
    signal req_c    : t_mem_req_32;
begin
    -- prioritize the first request found onto output
    process(reqs, inhibit)
    begin
        req_i <= c_mem_req_32_init;
        for i in reqs'range loop
            if reqs(i).request='1' then
                req_i <= reqs(i);
                exit;
            end if;
        end loop;
        if inhibit = '1' then
            req_i.request <= '0';
        end if;
    end process;

    -- send the reply to everyone (including tag)
    resps <= (others => resp);
    
    -- output register (will be eliminated when not used)
    process(clock)
    begin
        if rising_edge(clock) then
            req_c  <= req_i;
            if resp.rack = '1' and resp.rack_tag = req_c.tag then
                req_c.request <= '0';
            end if;
        end if;
    end process;

    req <= req_c when g_registered else req_i;

end architecture;
