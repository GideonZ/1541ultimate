--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_test1
-- Date:2015-01-27  
-- Author: Gideon     
-- Description: Testcase 3 for USB host
-- This testcase initializes a repeated IN transfer on Block Mode
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_bfm_pkg.all;
use work.tl_sctb_pkg.all;
use work.usb_cmd_pkg.all;
use work.tl_string_util_pkg.all;

entity usb_test_nano3 is

end entity;

architecture arch of usb_test_nano3 is
    signal clocks_stopped   : boolean := false;


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

    constant Block_Fifo_Base        : unsigned(19 downto 0) := X"00740"; -- 3A0 * 2
    constant Block_Fifo_Tail_Address: unsigned(19 downto 0) := X"007F4"; -- 3fa * 2
    constant Block_Fifo_Head_Address: unsigned(19 downto 0) := X"007F6"; -- 3fb * 2

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

        io_write_word(MemBlock_Base_High, X"0012");
        io_write_word(MemBlock_Base_Low,  X"6460"); -- 126460 is base :)

        io_write_word(Block_Fifo_Base + 0, X"0400");
        io_write_word(Block_Fifo_Base + 2, X"0600");
        io_write_word(Block_Fifo_Base + 4, X"0200");
        io_write_word(Block_Fifo_Base + 6, X"1000");
        io_write_word(Block_Fifo_Head_Address, X"0004"); -- 4 elements in fifo

        io_write_word(Pipe_DevEP,    X"0006"); -- EP4: big blocks
        io_write_word(Pipe_MaxTrans, X"0040");
        io_write_word(Pipe_Interval, X"0002"); -- every other microframe
        io_write_word(Pipe_Length,   X"0800"); -- MAX 2K
        io_write_word(Pipe_Command,  X"6042"); -- in with mem write, using block buffers

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
