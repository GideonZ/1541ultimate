

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.iec_bus_bfm_pkg.all;
use work.flat_memory_model.all;

entity testcase_format is
end testcase_format;

architecture tb of testcase_format is
begin
    test: process
        variable iec : p_iec_bus_bfm_object;
        variable ram : h_mem_object;
        variable msg : t_iec_message;
    begin
        bind_iec_bus_bfm(":testcase_format:harness:iec_bfm:", iec);
        bind_mem_model  (":testcase_format:harness:sram", ram);

--        wait for 1250 ms;  -- unpatched ROM
        wait for 30 ms;      -- patched ROM

        iec_send_atn(iec, X"28"); -- Drive 8, Listen (I will talk)
        iec_send_atn(iec, X"6F"); -- Open channel 15
        iec_send_message(iec, "N:HALLO,66");
        iec_send_atn(iec, X"3F"); -- Unlisten
        wait;                
    end process;
    
    harness: entity work.harness_1541;
end tb;
