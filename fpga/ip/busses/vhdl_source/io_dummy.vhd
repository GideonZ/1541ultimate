library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;

entity io_dummy is
port (
    clock       : in  std_logic;
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp );
end entity;

architecture dummy of io_dummy is
begin
    io_resp.data <= X"00";

    process(clock)
    begin
        if rising_edge(clock) then
            io_resp.ack <= io_req.read or io_req.write;
        end if;
    end process;
end dummy;

