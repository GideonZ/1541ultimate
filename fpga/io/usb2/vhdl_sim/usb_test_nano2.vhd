--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_test1
-- Date:2015-01-27  
-- Author: Gideon     
-- Description: Testcase 2 for USB host
-- This testcase initializes a repeated IN transfer in Circular Mem Buffer mode
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

entity usb_test_nano2 is
    generic (
        g_report_file_name : string := "work/usb_test_nano2.rpt"
    );

end entity;

architecture arch of usb_test_nano2 is
    signal clocks_stopped   : boolean := false;
    signal interrupt        : std_logic;
    
    constant Attr_Fifo_Base         : unsigned(19 downto 0) := X"00700"; -- 380 * 2 
    constant Attr_Fifo_Tail_Address : unsigned(19 downto 0) := X"007F0"; -- 3f8 * 2
    constant Attr_Fifo_Head_Address : unsigned(19 downto 0) := X"007F2"; -- 3f9 * 2

begin
    i_harness: entity work.usb_harness_nano
    port map (
        interrupt      => interrupt,
        clocks_stopped => clocks_stopped );

    process
        variable io : p_io_bus_bfm_object;
        variable mem : h_mem_object;
        variable res  : std_logic_vector(7 downto 0);
        variable attr_fifo_tail : integer := 0;
        variable attr_fifo_head : integer := 0;
        variable data   : std_logic_vector(15 downto 0);
        
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

        procedure read_attr_fifo(result : out std_logic_vector(15 downto 0)) is
            variable data   : std_logic_vector(15 downto 0);
        begin
            wait until interrupt = '1';
--            io_read_word(addr => Attr_Fifo_Head_Address, word => data);
--            attr_fifo_head := to_integer(unsigned(data));
--            L1: while true loop
--                io_read_word(addr => Attr_Fifo_Head_Address, word => data);
--                attr_fifo_head := to_integer(unsigned(data));
--                if (attr_fifo_head /= attr_fifo_tail) then
--                    exit L1;
--                end if;
--            end loop;
            io_read_word(addr => (Attr_Fifo_Base + attr_fifo_tail*2), word => data);
            attr_fifo_tail := attr_fifo_tail + 1;
            if attr_fifo_tail = 16 then
                attr_fifo_tail := 0;
            end if;
            io_write_word(addr => Attr_Fifo_Tail_Address, word => std_logic_vector(to_unsigned(attr_fifo_tail, 16)));
            sctb_trace("Fifo read: " & hstr(data));
            result := data;
        end procedure;

        procedure check_result(expected : std_logic_vector(7 downto 0); exp_result : std_logic_vector(15 downto 0)) is
            variable data : std_logic_vector(15 downto 0);
            variable byte : std_logic_vector(7 downto 0);
        begin
            io_read_word(Command_Length, data);
            sctb_trace("Command length: " & hstr(data));
            io_read_word(Command_Result, data);
            sctb_trace("Command result: " & hstr(data));
            sctb_check(data, exp_result, "Unexpected response");
            byte := read_memory_8(mem, X"00550000");
            sctb_check(byte, expected, "Erroneous byte");
            write_memory_8(mem, X"00550000", X"00");
        end procedure;
                
--        procedure wait_command_done is
--        begin
--            L1: while true loop
--                io_read(io => io, addr => Command, data => res);
--                if res(1) = '1' then -- check if paused bit has been set
--                    exit L1;
--                end if;
--            end loop;
--        end procedure;

    begin
        bind_io_bus_bfm("io", io);
        bind_mem_model("memory", mem);
        sctb_open_simulation("path::path", g_report_file_name);
        sctb_open_region("Testing Setup request", 0);
        sctb_set_log_level(c_log_level_trace);
        wait for 70 ns;
        io_write_word(c_nano_simulation, X"0001"  ); -- set nano to simulation mode
        io_write_word(c_nano_busspeed,   X"0002"  ); -- set bus speed to HS
        io_write(io, c_nano_enable, X"01"  ); -- enable nano
        wait for 4 us;

        io_write_word(Command_DevEP,    X"0007"); -- EP7: NAK NAK DATA0 NAK NAK DATA1 NAK STALL
        io_write_word(Command_MemHi,    X"0055");
        io_write_word(Command_MemLo,    X"0000");
        io_write_word(Command_MaxTrans, X"0010");
        io_write_word(Command_Interval, X"0002"); -- every other microframe
        io_write_word(Command_Length,   X"0010");

        -- arm
        io_write_word(Command_MemLo,    X"0000");
        io_write_word(Command,          X"5042"); -- in with mem write, using cercular buffer
        read_attr_fifo(data);
        check_result(X"44", X"8001");

        -- arm
        io_write_word(Command_MemLo,    X"0000");
        io_write_word(Command,          X"5842"); -- in with mem write, using cercular buffer
        read_attr_fifo(data);
        check_result(X"6B", X"8801");
            
        -- arm
        io_write_word(Command_MemLo,    X"0000");
        io_write_word(Command,          X"5042"); -- in with mem write, using cercular buffer
        read_attr_fifo(data);
        check_result(X"00", X"C400");

        sctb_close_region;

        sctb_close_simulation;
        clocks_stopped <= true;
        
        wait;
    end process;
        
end arch;
--restart; mem load -infile nano_code.hex -format hex /usb_test_nano2/i_harness/i_host/i_nano/i_buf_ram/mem; run 2000 us
