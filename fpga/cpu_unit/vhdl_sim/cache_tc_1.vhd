
library work;
use work.tl_flat_memory_model_pkg.all;
use work.mem_bus_master_bfm_pkg.all;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cache_tc_1 is
end;

architecture tc of cache_tc_1 is
    
    shared variable dram : h_mem_object;
    shared variable mm   : p_mem_bus_master_bfm_object;
begin
    i_harness: entity work.harness_dm_cache
        ;
        
	process
        variable read_data : std_logic_vector(7 downto 0);
	begin
        bind_mem_model("dram", dram);
        bind_mem_bus_master_bfm("mem_master", mm);

        wait for 100 ns;
        mem_write(mm, X"1200", X"01");
--        mem_write(mm, X"1201", X"02");
--        mem_write(mm, X"1202", X"03");
--        mem_write(mm, X"1203", X"04");
--        wait for 500 ns;
--        mem_read(mm, X"1200", read_data);
--        wait for 500 ns;
--        mem_read(mm, X"1203", read_data);
--        mem_read(mm, X"1202", read_data);
        wait for 500 ns;
--        mem_read(mm, X"5202", read_data);
        mem_write(mm, X"5202", X"33");
        wait for 500 ns;
        mem_read(mm, X"1203", read_data);
        mem_read(mm, X"1202", read_data);
        wait for 500 ns;

        mem_write(mm, X"0001", X"02");
        mem_write(mm, X"0002", X"03");
        mem_write(mm, X"0003", X"04");
        mem_write(mm, X"0004", X"05");
        mem_write(mm, X"0005", X"06");
        mem_write(mm, X"0006", X"07");
        mem_write(mm, X"0007", X"08");
        mem_write(mm, X"0008", X"09");
        mem_write(mm, X"0010", X"0A");
        mem_write(mm, X"0020", X"0B");
        mem_write(mm, X"0040", X"0C");
        mem_write(mm, X"0080", X"0D");
        mem_write(mm, X"0100", X"0E");
        mem_write(mm, X"0200", X"0F");
        mem_write(mm, X"0400", X"10");
        mem_write(mm, X"0800", X"11");
        mem_write(mm, X"1000", X"12");
        mem_write(mm, X"2000", X"13");
        mem_write(mm, X"4000", X"14");
        mem_write(mm, X"8000", X"15");

        mem_read(mm, X"0010", read_data);
        mem_read(mm, X"0020", read_data);
        mem_read(mm, X"0040", read_data);
        mem_read(mm, X"0080", read_data);
        mem_read(mm, X"0100", read_data);
        mem_read(mm, X"0200", read_data);
        mem_read(mm, X"0400", read_data);
        mem_read(mm, X"0800", read_data);
        mem_read(mm, X"1000", read_data);
        mem_read(mm, X"2000", read_data);
        mem_read(mm, X"4000", read_data);
        mem_read(mm, X"8000", read_data);
        mem_read(mm, X"0000", read_data);
        mem_read(mm, X"0001", read_data);
        mem_read(mm, X"0002", read_data);
        mem_read(mm, X"0003", read_data);
        mem_read(mm, X"0004", read_data);
        mem_read(mm, X"0005", read_data);
        mem_read(mm, X"0006", read_data);
        mem_read(mm, X"0007", read_data);
        mem_read(mm, X"0008", read_data);

		wait;
	end process;

end tc;
