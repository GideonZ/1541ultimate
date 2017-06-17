library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_bfm_pkg.all;
use work.tl_flat_memory_model_pkg.all;
use work.c1541_pkg.all;
use work.tl_string_util_pkg.all;

entity c1541_startup_tc is

end;

architecture tc of c1541_startup_tc is
begin

    i_harness: entity work.harness_c1541
        ;
    

    process
        variable io : p_io_bus_bfm_object;
        variable dram : h_mem_object;
        variable a, b, c, d : std_logic_vector(7 downto 0);
    begin
        wait for 1 ns;
        bind_io_bus_bfm("io_bfm", io);
        bind_mem_model("dram", dram);

        load_memory("../../../roms/1541-ii.bin", dram, X"0000C000");
        load_memory("./via.prg", dram, X"0000C020");
        load_memory("./via_load.prg", dram, X"0000C000");
        write_memory_16(dram, X"0000FFFC", X"C000"); 
        load_memory("../../../roms/sounds.bin", dram, X"00010000");

        wait for 30 us;
        io_write(io, c_drvreg_power, X"01");
        wait for 30 us;
        io_write(io, c_drvreg_reset, X"00");

        wait for 89 ms;
        a := read_memory_8(dram, X"00000100");
        b := read_memory_8(dram, X"00000101");
        c := read_memory_8(dram, X"00000102");
        d := read_memory_8(dram, X"00000103");
        
        report "Secret code is: " & hstr(a) & " " & hstr(b) & " " & hstr(c) & " " & hstr(d);
                
        wait;
    end process;
end architecture;
