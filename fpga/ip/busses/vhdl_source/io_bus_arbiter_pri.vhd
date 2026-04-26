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
    signal ext_read : std_logic_vector(0 to g_ports-1);
    signal ext_write: std_logic_vector(0 to g_ports-1);
    signal req_i    : t_io_req;
    signal select_i : integer range 0 to g_ports-1;
    signal select_c : integer range 0 to g_ports-1;
    type t_state is (idle, busy);
    signal state    : t_state;
begin
    -- extend the requests first, in order to make sure they won't get lost when more than one arrives at the same time
    -- This is only necessary for read and write pulses
    p_extend: process(clock)
    begin
        if rising_edge(clock) then
            for i in reqs'range loop
                -- clear has precedence
                if select_c = i and state = busy then
                    ext_read(i) <= '0';
                    ext_write(i) <= '0';
                else
                    if reqs(i).read = '1' then
                        ext_read(i) <= '1';
                    end if;
                    if reqs(i).write = '1' then
                        ext_write(i) <= '1';
                    end if;
                end if;
            end loop;
            if reset = '1' then
                ext_read <= (others => '0');
                ext_write <= (others => '0');
            end if;
        end if;
    end process;

    -- prioritize the first request found onto output
    process(reqs, ext_read, ext_write)
    begin
        req_i <= c_io_req_init;
        select_i <= 0;
        for i in reqs'range loop
            if reqs(i).read='1' or reqs(i).write='1' or ext_read(i) = '1' or ext_write(i) = '1' then
                req_i    <= reqs(i);
                req_i.read <= reqs(i).read or ext_read(i);
                req_i.write <= reqs(i).write or ext_write(i);
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
                req.read <= '0';
                req.write <= '0';
                if resp.ack='1' then
                    state <= idle;
                end if;                    

            when others =>
                null;
            end case;
        end if;
    end process;

    -- send the reply
    -- process(resp, select_c)
    -- begin
    --     for i in resps'range loop
    --         resps(i) <= resp;
    --         if i /= select_c then
    --             resps(i).ack <= '0';
    --         end if;
    --     end loop;
    -- end process;

    -- process(resp, select_c)
    -- begin
    --     for i in resps'range loop
    --         if i = select_c then
    --             resps(i) <= resp;
    --         else
    --             resps(i) <= c_io_resp_init;
    --         end if;
    --     end loop;
    -- end process;

    process(clock)
    begin
        if rising_edge(clock) then
            for i in resps'range loop
                if i = select_c then
                    resps(i) <= resp;
                else
                    resps(i) <= c_io_resp_init;
                end if;
            end loop;
        end if;
    end process;
end architecture;
