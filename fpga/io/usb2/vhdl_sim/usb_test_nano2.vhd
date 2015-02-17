--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_test1
-- Date:2015-01-27  
-- Author: Gideon     
-- Description: Testcase 2 for USB host
-- This testcase initializes a repeated IN transfer on 
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_bfm_pkg.all;
use work.tl_sctb_pkg.all;
use work.usb_cmd_pkg.all;
use work.tl_string_util_pkg.all;

entity usb_test_nano2 is

end entity;

architecture arch of usb_test_nano2 is
    signal clocks_stopped   : boolean := false;

--Offset_Command          = 0
--Offset_Command_DevEP    = 1
--Offset_Command_Length   = 2
--Offset_Command_MaxTrans = 3
--Offset_Command_Interval = 4
--Offset_Command_IntCount = 5
--Offset_Command_SplitCtl = 6
--Offset_Command_Result   = 7

--    constant Pipe_Command   : unsigned(19 downto 0) := X"005C0"; -- 2E0 * 2 
--    constant Pipe_DevEP     : unsigned(19 downto 0) := X"005C2";
--    constant Pipe_Length    : unsigned(19 downto 0) := X"005C4";
--    constant Pipe_MaxTrans  : unsigned(19 downto 0) := X"005C6";
--    constant Pipe_Interval  : unsigned(19 downto 0) := X"005C8";
--    constant Pipe_IntCount  : unsigned(19 downto 0) := X"005CA";
--    constant Pipe_SplitCtl  : unsigned(19 downto 0) := X"005CC";
--    constant Pipe_Result    : unsigned(19 downto 0) := X"005CE";

    constant Pipe_Command   : unsigned(19 downto 0) := X"005F0"; -- 2F8 * 2 (pipe 3) 
    constant Pipe_DevEP     : unsigned(19 downto 0) := X"005F2";
    constant Pipe_Length    : unsigned(19 downto 0) := X"005F4";
    constant Pipe_MaxTrans  : unsigned(19 downto 0) := X"005F6";
    constant Pipe_Interval  : unsigned(19 downto 0) := X"005F8";
    constant Pipe_IntCount  : unsigned(19 downto 0) := X"005FA";
    constant Pipe_SplitCtl  : unsigned(19 downto 0) := X"005FC";
    constant Pipe_Result    : unsigned(19 downto 0) := X"005FE";

    constant Attr_Fifo_Base         : unsigned(19 downto 0) := X"00640"; -- 320 * 2 
    constant Attr_Fifo_Tail_Address : unsigned(19 downto 0) := X"007F0"; -- 3f8 * 2
    constant Attr_Fifo_Head_Address : unsigned(19 downto 0) := X"007F2"; -- 3f9 * 2

    constant Circ_MemAddr_Start_High : unsigned(19 downto 0) := X"007C0"; -- 3e0
    constant Circ_MemAddr_Start_Low  : unsigned(19 downto 0) := X"007C2";
    constant Circ_Size               : unsigned(19 downto 0) := X"007C4";
    constant Circ_Offset             : unsigned(19 downto 0) := X"007C6";
    constant MemBlock_Base_High      : unsigned(19 downto 0) := X"007C8";
    constant MemBlock_Base_Low       : unsigned(19 downto 0) := X"007CA";

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
            io_write(io => io, addr => addr, data => word(7 downto 0));
            io_write(io => io, addr => (addr + 1), data => word(15 downto 8));
        end procedure;

        procedure io_read_word(addr : unsigned(19 downto 0); variable word : out std_logic_vector(15 downto 0)) is
        begin
            io_read(io => io, addr => addr, data => word(7 downto 0));
            io_read(io => io, addr => (addr + 1), data => word(15 downto 8));
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
            if attr_fifo_tail = 128 then
                attr_fifo_tail := 0;
            end if;
        end procedure;

--        procedure wait_command_done is
--        begin
--            L1: while true loop
--                io_read(io => io, addr => Command, data => res);
--                if res = X"00" then
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
        io_write(io => io, addr => X"007fe", data => X"01"  ); -- set nano to simulation mode
        io_write(io => io, addr => X"007fc", data => X"02"  ); -- set bus speed to HS
        io_write(io => io, addr => X"00800", data => X"01"  ); -- enable nano
        wait for 4 us;

        io_write_word(Circ_Size,     X"0040"); -- 64 bytes, only 4 entries and then we will wrap (test) 
        io_write_word(Circ_MemAddr_Start_High, X"0012");
        io_write_word(Circ_MemAddr_Start_Low,  X"6460"); -- 126460 is base :)

        io_write_word(Pipe_DevEP,    X"0007"); -- EP7: NAK NAK DATA0 NAK NAK DATA1 NAK STALL
        io_write_word(Pipe_MaxTrans, X"0010");
        io_write_word(Pipe_Interval, X"0002"); -- every other microframe
        io_write_word(Pipe_Length,   X"0010");
        io_write_word(Pipe_Command,  X"5042"); -- in with mem write, using cercular buffer

        for i in 1 to 16 loop
            read_attr_fifo(data);
            sctb_trace("Fifo read: " & hstr(data));
        end loop;

        wait for 1 ms;

        sctb_close_region;
        clocks_stopped <= true;
        
        wait;
    end process;
        
end arch;
