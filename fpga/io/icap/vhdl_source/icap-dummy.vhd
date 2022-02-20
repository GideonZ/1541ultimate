library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.icap_pkg.all;

entity icap is
generic (
    g_enable    : boolean := false );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp );

end icap;

architecture dummy of icap is
begin
    assert not g_enable
        report "The dummy ICAP module cannot be enabled; it is just a dummy!"
        severity failure;
         
    i_dummy: entity work.io_dummy
    port map(
        clock   => clock,
        io_req  => io_req,
        io_resp => io_resp
    );
    
end architecture;
