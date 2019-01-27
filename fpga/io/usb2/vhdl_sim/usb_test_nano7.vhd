--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_test1
-- Date:2015-01-27  
-- Author: Gideon     
-- Description: Testcase 7 for USB host
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

entity usb_test_nano7 is
    generic (
        g_report_file_name : string := "work/usb_test_nano7.rpt"
    );
end entity;

architecture arch of usb_test_nano7 is
    signal clocks_stopped   : boolean := false;
    signal interrupt        : std_logic;
    signal nyet_count       : natural := 2;
    signal ack_on_ping      : boolean := true;
    constant Attr_Fifo_Base         : unsigned(19 downto 0) := X"00700"; -- 380 * 2 
    constant Attr_Fifo_Tail_Address : unsigned(19 downto 0) := X"007F0"; -- 3f8 * 2
    constant Attr_Fifo_Head_Address : unsigned(19 downto 0) := X"007F2"; -- 3f9 * 2
begin
    i_harness: entity work.usb_harness_nano
    port map (
        ack_on_ping    => ack_on_ping,
        nyet_count     => nyet_count,
        interrupt      => interrupt,
        clocks_stopped => clocks_stopped );

    process
        variable io : p_io_bus_bfm_object;
        variable mem : h_mem_object;
        variable data : std_logic_vector(15 downto 0);
        variable res  : std_logic_vector(7 downto 0);
        variable pipe : integer;
        variable attr_fifo_tail : integer := 0;
        variable attr_fifo_head : integer := 0;
                
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

        procedure read_attr_fifo(result : out std_logic_vector(15 downto 0); timeout : time) is
            variable data   : std_logic_vector(15 downto 0);
            variable cmd    : std_logic_vector(15 downto 0);
            variable res    : std_logic_vector(15 downto 0);
        begin
            wait until interrupt = '1' for timeout;
            if interrupt = '0' then
                sctb_trace("Timeout waiting for interrupt.");
                result := X"FFFF";
                return;
            end if;
            io_read_word(addr => (Attr_Fifo_Base + attr_fifo_tail*2), word => data);
            attr_fifo_tail := attr_fifo_tail + 1;
            if attr_fifo_tail = 16 then
                attr_fifo_tail := 0;
            end if;
            io_write_word(addr => Attr_Fifo_Tail_Address, word => std_logic_vector(to_unsigned(attr_fifo_tail, 16)));
            io_read_word(addr => Command, word => cmd);
            io_read_word(addr => Command_Result, word => res);
            sctb_trace("Fifo read: " & hstr(data) & ", Command: " & hstr(cmd) & ", Result: " & hstr(res));
            result := res;
        end procedure;

    begin
        bind_io_bus_bfm("io", io);
        bind_mem_model("memory", mem);
        sctb_open_simulation("path:path", g_report_file_name);
        sctb_set_log_level(c_log_level_trace);
        wait for 70 ns;
        io_write_word(c_nano_simulation, X"0001"  ); -- set nano to simulation mode
        io_write_word(c_nano_busspeed,   X"0002"  ); -- set bus speed to HS
        io_write(io, c_nano_enable, X"01"  ); -- enable nano
        wait for 4 us;

        sctb_open_region("Testing an out packet that NAKs, without SPLIT, using a timeout of 10 frames.", 0);

        --io_write_word(Command_SplitCtl, X"8130"); -- Hub Address 1, Port 2, Speed = FS, EP = control 
        io_write_word(Command_SplitCtl, X"0000"); -- High speed 
        io_write_word(Command_DevEP,    X"0002");
        io_write_word(Command_MaxTrans, X"0010");
        io_write_word(Command_Length,   X"0010");
        io_write_word(Command_MemHi,    X"0005");
        io_write_word(Command_MemLo,    X"0000");
        io_write_word(Command_Interval, X"0000");
        io_write_word(Command_Timeout,  X"000A");
        
        io_write_word(c_nano_numpipes,   X"0001"  ); -- Set active pipes to 1
        io_write_word(Command, X"8041"); -- 8000 = read from memory, 40 = Do Data, 01 = OUT

        -- There should be NO report until the timeout has occurred.

        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"AC00", "Result should be AC00, namely: NAK + Timeout");
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;

        sctb_open_region("Testing an out packet that NAKs, with SPLIT, using a timeout of 10 frames.", 0);
        -- Enable split, and try the same thing
        io_write_word(Command_SplitCtl, X"8130"); -- Hub Address 1, Port 2, Speed = FS, EP = control 
        -- re-enable the command, but we do need to clear the start flag too
        io_write_word(Command_Started, X"0000");
        io_write_word(Command, X"8041"); -- 8000 = read from memory, 40 = Do Data, 01 = OUT
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"AC00", "Result should be AC00, namely: NAK + Timeout");
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;


        sctb_open_region("Testing an in packet that NAKs, without SPLIT, using a timeout of 10 frames.", 0);
        io_write_word(Command_SplitCtl, X"0000"); -- High speed 
        io_write_word(Command_Started, X"0000");
        io_write_word(Command, X"4042"); -- 4000 = write to memory, 40 = Do Data, 2 = IN
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"AC00", "Result should be AC00, namely: NAK + Timeout");
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;


        sctb_open_region("Testing an in packet that NAKs, with SPLIT, using a timeout of 10 frames.", 0);
        io_write_word(Command_SplitCtl, X"8130"); -- Hub Address 1, Port 2, Speed = FS, EP = control 
        io_write_word(Command_Started, X"0000");
        io_write_word(Command, X"4042"); -- 4000 = write to memory, 40 = Do Data, 2 = IN
        read_attr_fifo(data, 2.0 ms);
        sctb_check(data, X"AC00", "Result should be AC00, namely: NAK + Timeout");
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;

        sctb_open_region("Testing an out packet that NYETs too often, with SPLIT, using a timeout of 10 frames.", 0);
        nyet_count <= 12;
        io_write_word(Command_SplitCtl, X"8130"); -- Hub Address 1, Port 2, Speed = FS, EP = control 
        -- re-enable the command, but we do need to clear the start flag too
        io_write_word(Command_Started, X"0000");
        io_write_word(Command, X"8041"); -- 8000 = read from memory, 40 = Do Data, 01 = OUT
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"B400", "Result should be B400, namely: NYET");
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;


        sctb_open_region("Testing an out packet that always NAKs on PING in HS mode, using a timeout of 10 frames.", 0);

        io_write_word(Command_SplitCtl, X"0000"); -- High speed 
        io_write_word(Command_Started, X"0000");
        ack_on_ping <= false; -- !!
        io_write_word(Command, X"8041"); -- 8000 = read from memory, 40 = Do Data, 01 = OUT
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"AC00", "Result should be AC00, namely: NAK + Timeout");
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;

        sctb_open_region("Testing an out packet that always NAKs in HS mode, no timeout, but manual abort.", 0);

        io_write_word(Command_SplitCtl, X"0000"); -- High speed 
        io_write_word(Command_Started, X"0000");
        io_write_word(Command_Timeout,  X"0000");
        ack_on_ping <= true;

        io_write_word(Command, X"8041"); -- 8000 = read from memory, 40 = Do Data, 01 = OUT
        wait for 0.5 ms;
        io_write_word(Command, X"8141"); -- Abort current command

        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"6400", "Result should be 6400, namely: Aborted!");
        read_attr_fifo(data, 1.5 ms);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;

        sctb_close_simulation;
        clocks_stopped <= true;
        
        wait;
    end process;
        
end arch;

-- restart; mem load -infile nano_code.hex -format hex /usb_test_nano7/i_harness/i_host/i_nano/i_buf_ram/mem; run 20 ms