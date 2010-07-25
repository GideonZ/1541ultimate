

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.iec_bus_bfm_pkg.all;
use work.flat_memory_model.all;

entity testcase_init is
end testcase_init;

architecture tb of testcase_init is
begin
    test: process
        variable iec : p_iec_bus_bfm_object;
        variable ram : h_mem_object;
        variable msg : t_iec_message;
    begin
        bind_iec_bus_bfm(":testcase_init:harness:iec_bfm:", iec);
        bind_mem_model  (":testcase_init:harness:sram", ram);

--        wait for 1250 ms;  -- unpatched ROM
        wait for 30 ms;      -- patched ROM

        iec_send_atn(iec, X"48"); -- Drive 8, Talk, I will listen
        iec_send_atn(iec, X"6F"); -- Open channel 15
        iec_turnaround(iec);      -- start to listen
        iec_get_message(iec, msg);
        iec_print_message(msg);
        wait;                
    end process;
    
    harness: entity work.harness_1541;
end tb;
