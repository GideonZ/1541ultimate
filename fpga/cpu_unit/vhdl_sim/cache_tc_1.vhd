
library work;
use work.tl_flat_memory_model_pkg.all;
use work.mem_bus_master_bfm_pkg.all;
use work.tl_string_util_pkg.all;

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
        procedure check_mem(addr : unsigned; expected : std_logic_vector) is
            variable rd_data : std_logic_vector(7 downto 0);
        begin
            mem_read(mm, addr, rd_data);
            assert rd_data = expected
                report "Unexpected data on client " & hstr(addr) & ": Got " & hstr(rd_data) & " while " & hstr(expected) & " was expected."
                severity error;
        end procedure;
        procedure check_dram(addr : unsigned; expected : std_logic_vector) is
            variable rd_data : std_logic_vector(7 downto 0);
            variable a       : std_logic_vector(31 downto 0);
        begin
            a := (others => '0');
            a(addr'length-1 downto 0) := std_logic_vector(addr);
            rd_data := read_memory_8(dram, a);
            assert rd_data = expected
                report "Unexpected data in dram " & hstr(addr) & ": Got " & hstr(rd_data) & " while " & hstr(expected) & " was expected."
                severity error;
        end procedure;
	begin
        bind_mem_model("dram", dram);
        bind_mem_bus_master_bfm("mem_master", mm);

        wait for 100 ns;
        
        for i in 0 to 254 loop
            write_memory_8(dram, std_logic_vector(to_unsigned(i,32)), std_logic_vector(to_unsigned(i+1,8)));
        end loop;

        -- check if single entry gets written to dram, when cache line is empty
        check_dram(X"0002", X"03"); -- see if the loop is correct
        mem_write(mm, X"1802", X"55");
        check_mem(X"0002", X"03"); -- written in the loop above, same cacheline!
        wait for 200 ns;
        check_dram(X"1802", X"55");

        -- check if modified cacheline gets written back correctly upon read miss
        check_mem(X"0010", X"11"); -- written in loop
        mem_write(mm, X"0011", X"FB");
        check_dram(X"0011", X"12"); -- should be OLD data!
        mem_read(mm, X"7010", read_data); -- causes read miss
        wait for 200 ns;
        check_dram(X"0010", X"11");
        check_dram(X"0011", X"FB");
        check_dram(X"0012", X"13");
        check_dram(X"0013", X"14");

        -- check if modified cacheline gets written back correctly upon write miss
        check_mem(X"0030", X"31"); -- written in loop
        mem_write(mm, X"0031", X"FC");
        check_dram(X"0031", X"32"); -- should be OLD data!
        mem_write(mm, X"7031", X"99"); -- causes write miss
        wait for 400 ns;
        check_dram(X"0030", X"31");
        check_dram(X"0031", X"FC");
        check_dram(X"0032", X"33");
        check_dram(X"0033", X"34");
        
        -- check if a cacheline is not dirty, that it does not get written back
        check_mem(X"0060", X"61"); -- cache line fill (cause: cache miss)
        wait for 200 ns;
        write_memory_8(dram, X"00000062", X"FF"); -- modify dram
        mem_read(mm, X"3060", read_data); -- cache line fill again 
        wait for 200 ns;
        check_dram(X"0062", X"FF");

        -- other checks        
        
        mem_write(mm, X"1080", X"01");
        mem_write(mm, X"1081", X"02");
        mem_write(mm, X"1082", X"03");
        mem_write(mm, X"1083", X"04");
        report "Checking data that just got written in a hit..";
        check_mem(X"1083", X"04");
        check_mem(X"1082", X"03");
        check_mem(X"5082", X"00");
        mem_write(mm, X"5082", X"33");
        check_mem(X"5082", X"33");
        mem_read(mm, X"0081", read_data);
        check_mem(X"5082", X"33");

        mem_write(mm, X"0000", X"01");
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

        check_mem(X"0010", X"0A");
        check_mem(X"0020", X"0B");
        check_mem(X"0040", X"0C");
        check_mem(X"0080", X"0D");
        check_mem(X"0100", X"0E");
        check_mem(X"0200", X"0F");
        check_mem(X"0400", X"10");
        check_mem(X"0800", X"11");
        check_mem(X"1000", X"12");
        check_mem(X"2000", X"13");
        check_mem(X"4000", X"14");
        check_mem(X"8000", X"15");
        check_mem(X"0000", X"01");
        check_mem(X"0001", X"02");
        check_mem(X"0002", X"03");
        check_mem(X"0003", X"04");
        check_mem(X"0004", X"05");
        check_mem(X"0005", X"06");
        check_mem(X"0006", X"07");
        check_mem(X"0007", X"08");
        check_mem(X"0008", X"09");

		wait;
	end process;

end tc;
