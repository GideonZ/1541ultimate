library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_bfm_pkg.all;
use work.tl_flat_memory_model_pkg.all;
use work.c1541_pkg.all;

entity c1541_startup_tc is

end;

architecture tc of c1541_startup_tc is
begin

    i_harness: entity work.harness_c1541
        ;
    
    process
        variable io : p_io_bus_bfm_object;
    begin
        wait for 1 ns;
        bind_io_bus_bfm("io_bfm", io);

        wait for 30 us;
        io_write(io, c_drvreg_power, X"01");
        wait for 30 us;
        io_write(io, c_drvreg_reset, X"00");
        wait;
    end process;
end architecture;
