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

entity usb_test_nano1 is

end entity;

architecture arch of usb_test_nano1 is
    signal clocks_stopped   : boolean := false;

    constant Command          : unsigned(19 downto 0) := X"007E0";
    constant Command_DevEP    : unsigned(19 downto 0) := X"007E2";
    constant Command_Length   : unsigned(19 downto 0) := X"007E4";
    constant Command_MaxTrans : unsigned(19 downto 0) := X"007E6";
    constant Command_MemHi    : unsigned(19 downto 0) := X"007E8";
    constant Command_MemLo    : unsigned(19 downto 0) := X"007EA";
    constant Command_SplitCtl : unsigned(19 downto 0) := X"007EC";
    constant Command_Result   : unsigned(19 downto 0) := X"007EE";

    constant c_rx_size  : integer := 4096 + 512;
    constant c_tx_size  : integer := 4096 + 512;
begin
    i_harness: entity work.usb_harness_nano
    port map (
        clocks_stopped => clocks_stopped );

    process
        variable io : p_io_bus_bfm_object;
        variable data : std_logic_vector(15 downto 0);
        variable res  : std_logic_vector(7 downto 0);
        variable start, stop : time;
        variable transferred : integer;
        variable micros, kbps   : real;
        
        procedure io_write_word(addr : unsigned(19 downto 0); word : std_logic_vector(15 downto 0)) is
        begin
            io_write(io => io, addr => (addr + 1), data => word(7 downto 0));
            io_write(io => io, addr => (addr + 0), data => word(15 downto 8));
        end procedure;

        procedure io_read_word(addr : unsigned(19 downto 0); word : out std_logic_vector(15 downto 0)) is
        begin
            io_read(io => io, addr => (addr + 1), data => word(7 downto 0));
            io_read(io => io, addr => (addr + 0), data => word(15 downto 8));
        end procedure;

        procedure wait_command_done is
        begin
            L1: while true loop
                io_read(io => io, addr => Command, data => res);
                if res = X"00" then
                    exit L1;
                end if;
            end loop;
        end procedure;
    begin
        bind_io_bus_bfm("io", io);
        sctb_open_simulation("path:path", "usb_test_nano1.tcr");
        sctb_open_region("Testing Setup request", 0);
        sctb_set_log_level(c_log_level_trace);
        wait for 70 ns;
        io_write(io => io, addr => X"007fe", data => X"01"  ); -- set nano to simulation mode
        io_write(io => io, addr => X"007fd", data => X"02"  ); -- set bus speed to HS
        io_write(io => io, addr => X"00800", data => X"01"  ); -- enable nano
        wait for 4 us;

        io_write_word(Command_DevEP,    X"0000");
        io_write_word(Command_MaxTrans, X"0040");
        io_write_word(Command_MemHi,    X"0004");
        io_write_word(Command_MemLo,    X"132A");
        io_write_word(Command_Length,   X"0008");
        io_write_word(Command,          X"8040"); -- setup with mem read

        wait_command_done;
        sctb_close_region;

        sctb_open_region("Testing In request", 0);

        io_write_word(Command_DevEP,    X"0006");
        io_write_word(Command_Length,   X"0FFF");
        io_write_word(Command,          X"4042"); -- in with mem write
        wait_command_done;

        io_read_word(Command_Result, data);
        sctb_trace("Command result: " & hstr(data));
        io_read_word(Command_Length, data);
        transferred := 4095 - to_integer(signed(data));
        sctb_trace("Transferred: " & integer'image(transferred));
        sctb_assert(transferred = 139, "Expected 4096 bytes.");

        start := now;
        
        io_write_word(Command_MaxTrans, X"0200");
        io_write_word(Command_DevEP,    X"0004");
        io_write_word(Command_Length,   std_logic_vector(to_unsigned(c_rx_size, 16)));
        io_write_word(Command,          X"4042"); -- in with mem write

        wait_command_done;

        stop := now;

        io_read_word(Command_Result, data);
        sctb_trace("Command result: " & hstr(data));

        io_read_word(Command_Length, data);
        transferred := c_rx_size - to_integer(signed(data));
        sctb_trace("Transferred: " & integer'image(transferred));
        sctb_check(transferred, c_rx_size, "Expected c_rx_size bytes.");

        micros := real((stop - start) / 1.0 us);
        kbps := (real(transferred) * 1000.0) / micros;
        sctb_trace("Got " & integer'image(integer(kbps)) & " KB/s");

        sctb_close_region;

        sctb_open_region("Testing Out request", 0);

        start := now;
        io_write_word(Command_MemHi,    X"0001");
        io_write_word(Command_MemLo,    X"0402");
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
