library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;

entity io_bus_splitter is
generic (
    g_range_lo  : natural := 16;
    g_range_hi  : natural := 19;
    g_ports     : positive := 3 );
port (
    clock   : in  std_logic;
    
    req     : in  t_io_req;
    resp    : out t_io_resp;
    
    reqs    : out t_io_req_array(0 to g_ports-1);
    resps   : in  t_io_resp_array(0 to g_ports-1) );
end io_bus_splitter;

architecture rtl of io_bus_splitter is
    constant c_max_ports    : integer := 2 ** (1 + g_range_hi - g_range_lo);
    signal   dummy_resp     : t_io_resp;
begin
    -- sanity check
    assert g_ports <= c_max_ports
        report "Number of ports exceeds decoding capacity of address vector."
        severity failure;
    
    -- combinatioral decode
    process(req)
    begin
        for i in reqs'range loop
            reqs(i) <= req;
            if to_integer(req.address(g_range_hi downto g_range_lo)) = i then
                reqs(i).write <= req.write;
                reqs(i).read  <= req.read;
            else
                reqs(i).write <= '0';
                reqs(i).read <= '0';
            end if;
        end loop;
    end process;
    
    -- prevent hangup from incomplete decode
    process(clock)
    begin
        if rising_edge(clock) then
            dummy_resp <= c_io_resp_init;
            if to_integer(req.address(g_range_hi downto g_range_lo)) >= g_ports then
                dummy_resp.ack <= req.write or req.read;
            end if;
        end if;
    end process;
    
    -- combine responses to one, back to master.
    resp <= or_reduce(resps & dummy_resp);
end rtl;
