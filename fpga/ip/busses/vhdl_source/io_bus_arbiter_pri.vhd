library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity io_bus_arbiter_pri is
generic (
    g_ports     : positive := 3 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    reqs        : in  t_io_req_array(0 to g_ports-1);
    resps       : out t_io_resp_array(0 to g_ports-1);
    
    req         : out t_io_req;
    resp        : in  t_io_resp );
end entity;

architecture rtl of io_bus_arbiter_pri is
    signal req_i    : t_io_req;
    signal select_i : integer range 0 to g_ports-1;
    signal select_c : integer range 0 to g_ports-1;
    type t_state is (idle, busy);
    signal state    : t_state;
begin
    -- prioritize the first request found onto output
    process(reqs)
    begin
        req_i <= c_io_req_init;
        select_i <= 0;
        for i in reqs'range loop
            if reqs(i).read='1' or reqs(i).write='1' then
                req_i    <= reqs(i);
                select_i <= i;
                exit;
            end if;
        end loop;
    end process;

    p_access: process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                req <= req_i;
                if req_i.read='1' then
                    select_c <= select_i;
                    state <= busy;
                elsif req_i.write='1' then
                    select_c <= select_i;
                    state <= busy;
                end if;
            
            when busy =>
				req <= reqs(select_c);
                if resp.ack='1' then
                    state <= idle;
                end if;                    

            when others =>
                null;
            end case;
        end if;
    end process;

    -- send the reply to everyone, but mask the acks to non-active clients
    process(resp, select_c)
    begin
        for i in resps'range loop
            resps(i) <= resp;
            if i /= select_c then
                resps(i).ack <= '0';
            end if;
        end loop;
    end process;

end architecture;
