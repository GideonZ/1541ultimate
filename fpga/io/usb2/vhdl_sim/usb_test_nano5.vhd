--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_test1
-- Date:2015-01-27  
-- Author: Gideon     
-- Description: Testcase 1 for USB host
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_bfm_pkg.all;
use work.tl_sctb_pkg.all;
use work.usb_cmd_pkg.all;
use work.tl_string_util_pkg.all;
use work.nano_addresses_pkg.all;
use work.tl_flat_memory_model_pkg.all;

entity usb_test_nano5 is
    generic (
        g_report_file_name : string := "work/usb_test_nano5.rpt"
    );
end entity;

architecture arch of usb_test_nano5 is
    signal clocks_stopped   : boolean := false;

    constant c_rx_size  : integer := 4096 + 512;
    constant c_tx_size  : integer := 4096 + 512;
begin
    i_harness: entity work.usb_harness_nano
    port map (
        clocks_stopped => clocks_stopped );

    process
        variable io : p_io_bus_bfm_object;
        variable mem : h_mem_object;
        variable data : std_logic_vector(15 downto 0);
        variable res  : std_logic_vector(7 downto 0);
        variable start, stop : time;
        variable transferred : integer;
        variable micros, kbps   : real;
        
        procedure io_write_word(addr : unsigned(19 downto 0); word : std_logic_vector(15 downto 0)) is
        begin
            io_write(io => io, addr => (addr + 0), data => word(7 downto 0));
            io_write(io => io, addr => (addr + 1), data => word(15 downto 8));
        end procedure;

        procedure io_read_word(addr : unsigned(19 downto 0); word : out std_logic_vector(15 downto 0)) is
        begin
            io_read(io => io, addr => (addr + 0), data => word(7 downto 0));
            io_read(io => io, addr => (addr + 1), data => word(15 downto 8));
        end procedure;

        procedure wait_command_done is
        begin
            L1: while true loop
                io_read(io => io, addr => Command+1, data => res);
                if res(1) = '1' then -- check if paused bit has been set
                    exit L1;
                end if;
            end loop;
        end procedure;
    begin
        bind_io_bus_bfm("io", io);
        bind_mem_model("memory", mem);
        sctb_open_simulation("path:path", g_report_file_name);
        sctb_open_region("Testing Setup request", 0);
        sctb_set_log_level(c_log_level_trace);
        wait for 70 ns;
        io_write_word(c_nano_simulation, X"0001"  ); -- set nano to simulation mode
        io_write_word(c_nano_busspeed,   X"0002"  ); -- set bus speed to HS
        io_write(io, c_nano_enable, X"01"  ); -- enable nano
        wait for 4 us;

        write_memory_8(mem, X"00041328", X"11");        
        write_memory_8(mem, X"00041329", X"22");        
        write_memory_8(mem, X"0004132A", X"33");        
        write_memory_8(mem, X"0004132B", X"44");        
        write_memory_8(mem, X"0004132C", X"55");        
        write_memory_8(mem, X"0004132D", X"66");        
        write_memory_8(mem, X"0004132E", X"77");        
        write_memory_8(mem, X"0004132F", X"88");        

        io_write_word(Command_SplitCtl, X"8132"); -- Hub Address 1, Port 2, Speed = FS, EP = interrupt 
        io_write_word(Command_DevEP,    X"0000");
        io_write_word(Command_MaxTrans, X"0040");
        io_write_word(Command_MemHi,    X"0004");
        io_write_word(Command_MemLo,    X"1328");
        io_write_word(Command_Length,   X"0008");
        io_write_word(Command_Timeout,  X"0444");
        io_write_word(Command_Started,  X"0000");
        io_write_word(Command,          X"8040"); -- setup with mem read

        wait_command_done;
        io_read_word(Command_Result, data);
        sctb_trace("Command result: " & hstr(data));
        io_read_word(Command_Length, data);
        sctb_trace("Command length: " & hstr(data));
        sctb_close_region;

        sctb_open_region("Testing In request", 0);

        start := now;
        io_write_word(Command_DevEP,    X"0004");
        io_write_word(Command_MaxTrans, X"0100");
        io_write_word(Command_Length,   X"0FFF");
        io_write_word(Command_Started,  X"0000");
        io_write_word(Command,          X"4042"); -- in with mem write
        wait_command_done;
        stop := now;

        io_read_word(Command_Result, data);
        sctb_trace("Command result: " & hstr(data));
        io_read_word(Command_Length, data);
        sctb_trace("Command length: " & hstr(data));
        transferred := 4095 - to_integer(signed(data));
        sctb_trace("Transferred: " & integer'image(transferred));
        sctb_assert(transferred = 4096, "Expected 4096 bytes.");

        micros := real((stop - start) / 1.0 us);
        kbps := (real(transferred) * 1000.0) / micros;
        sctb_trace("Got " & integer'image(integer(kbps)) & " KB/s");

        sctb_close_region;

        wait for 20 us;

        sctb_open_region("Testing Out request", 0);
        start := now;
        io_write_word(Command_MemHi,    X"0004");
        io_write_word(Command_MemLo,    X"1328");
        io_write_word(Command_MaxTrans, X"0200");
        io_write_word(Command_DevEP,    X"0005");
        io_write_word(Command_Length,   std_logic_vector(to_unsigned(c_tx_size, 16)));
        io_write_word(Command,          X"8041"); -- out with mem read
        wait_command_done;
        stop := now;

        io_read_word(Command_Result, data);
        sctb_trace("Command result: " & hstr(data));

        io_read_word(Command_Length, data);
        transferred := c_tx_size - to_integer(signed(data));
        sctb_trace("Transferred: " & integer'image(transferred));
        sctb_check(transferred, c_tx_size, "Expected to send c_tx_size bytes.");

        micros := real((stop - start) / 1.0 us);
        kbps := (real(transferred) * 1000.0) / micros;
        sctb_trace("Got " & integer'image(integer(kbps)) & " KB/s");

        sctb_close_region;

        sctb_close_simulation;
        clocks_stopped <= true;
        
        wait;
    end process;
        
end arch;

-- restart; mem load -infile nano_code.hex -format hex /usb_test_nano5/i_harness/i_host/i_nano/i_buf_ram/mem; run 2000 us