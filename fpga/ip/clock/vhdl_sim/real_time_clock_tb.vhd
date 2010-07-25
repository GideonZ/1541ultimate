library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity real_time_clock_tb is
end;

architecture tb of real_time_clock_tb is
    signal clock        : std_logic := '0';
    signal reset        : std_logic := '0';
    signal req          : t_io_req  := c_io_req_init;
    signal resp         : t_io_resp := c_io_resp_init;
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_dut: entity work.real_time_clock
    generic map (
        g_freq      => 100 )
    port map (
        clock       => clock,    
        reset       => reset,
        
        req         => req,
        resp        => resp );

end;
