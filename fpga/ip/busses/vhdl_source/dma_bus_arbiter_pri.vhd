library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.dma_bus_pkg.all;

entity dma_bus_arbiter_pri is
generic (
    g_ports     : positive := 3 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    reqs        : in  t_dma_req_array(0 to g_ports-1);
    resps       : out t_dma_resp_array(0 to g_ports-1);
    
    req         : out t_dma_req;
    resp        : in  t_dma_resp );
end entity;

architecture rtl of dma_bus_arbiter_pri is
    signal req_i    : t_dma_req;
    signal select_i : integer range 0 to g_ports-1;
    signal select_c : integer range 0 to g_ports-1;
    type t_state is (idle, busy_read, busy_write);
    signal state    : t_state;
begin
    -- prioritize the first request found onto output
    process(reqs)
    begin
        req_i <= c_dma_req_init;
        select_i <= 0;
        for i in reqs'range loop
            if reqs(i).request='1' then
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
                if req_i.request='1' then
                    select_c <= select_i;
                    if req_i.read_writen='0' then
                        state <= busy_write;
                    else
                        state <= busy_read;
                    end if;
                end if;
            
            when busy_read =>
                if resp.rack='1' then
                    req.request <= '0';
                end if;                    
                if resp.dack='1' then
                    state <= idle;
                end if;
            
            when busy_write =>
                if resp.rack='1' then
                    req.request <= '0';
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
                resps(i).rack <= '0';
                resps(i).dack <= '0';
            end if;
        end loop;
    end process;

end architecture;
