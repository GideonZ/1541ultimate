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

entity usb_test_nano2 is

end entity;

architecture arch of usb_test_nano2 is
    signal clocks_stopped   : boolean := false;

    constant Attr_Fifo_Base         : unsigned(19 downto 0) := X"00700"; -- 380 * 2 
    constant Attr_Fifo_Tail_Address : unsigned(19 downto 0) := X"007F0"; -- 3f8 * 2
    constant Attr_Fifo_Head_Address : unsigned(19 downto 0) := X"007F2"; -- 3f9 * 2

begin
    i_harness: entity work.usb_harness_nano
    port map (
        clocks_stopped => clocks_stopped );

    process
        variable io : p_io_bus_bfm_object;
        variable data : std_logic_vector(15 downto 0);
        variable res  : std_logic_vector(7 downto 0);
        variable attr_fifo_tail : integer := 0;
        variable attr_fifo_head : integer := 0;
        
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

        procedure read_attr_fifo(result : out std_logic_vector(15 downto 0)) is
            variable data   : std_logic_vector(15 downto 0);
        begin
            L1: while true loop
                io_read_word(addr => Attr_Fifo_Head_Address, word => data);
                attr_fifo_head := to_integer(unsigned(data));
                if (attr_fifo_head /= attr_fifo_tail) then
                    exit L1;
                end if;
            end loop;
            io_read_word(addr => (Attr_Fifo_Base + attr_fifo_tail*2), word => result);
            attr_fifo_tail := attr_fifo_tail + 1;
            if attr_fifo_tail = 16 then
                attr_fifo_tail := 0;
            end if;
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
        sctb_open_simulation("path::path", "usb_test_nano2.tcr");
        sctb_open_region("Testing Setup request", 0);
        sctb_set_log_level(c_log_level_trace);
        wait for 70 ns;
        io_write_word(c_nano_simulation, X"0001"  ); -- set nano to simulation mode
        io_write_word(c_nano_busspeed,   X"0002"  ); -- set bus speed to HS
        io_write(io, c_nano_enable, X"01"  ); -- enable nano
        wait for 4 us;

        io_write_word(Command_DevEP,    X"0007"); -- EP7: NAK NAK DATA0 NAK NAK DATA1 NAK STALL
        io_write_word(Command_MemHi,    X"0055");
        io_write_word(Command_MaxTrans, X"0010");
        io_write_word(Command_Interval, X"0002"); -- every other microframe
        io_write_word(Command_Length,   X"0010");
        io_write_word(Command,          X"5042"); -- in with mem write, using cercular buffer

        for i in 1 to 16 loop
            read_attr_fifo(data);
            sctb_trace("Fifo read: " & hstr(data));
            io_read_word(Command_Length, data);
            sctb_trace("Command length: " & hstr(data));
            io_read_word(Command_Result, data);
            sctb_trace("Command result: " & hstr(data));
            if data(11) = '0' then
                io_write_word(Command, X"5842"); -- in with mem write
            else
                io_write_word(Command, X"5042"); -- in with mem write
            end if;
        end loop;

        wait for 1 ms;

        sctb_close_region;
        clocks_stopped <= true;
        
        wait;
    end process;
        
end arch;
