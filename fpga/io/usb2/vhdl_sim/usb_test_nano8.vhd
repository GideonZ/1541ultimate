--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_test1
-- Date:2019-01-27  
-- Author: Gideon     
-- Description: Testcase 8 for USB host
-- This testcase verfies the correct behavior of the short packets
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

entity usb_test_nano8 is
    generic (
        g_report_file_name : string := "work/usb_test_nano8.rpt"
    );
end entity;

architecture arch of usb_test_nano8 is
    signal clocks_stopped   : boolean := false;
    signal interrupt        : std_logic;
    signal nyet_count       : natural := 2;
    signal ack_on_ping      : boolean := true;
    signal transfer_size    : natural := 256;
    signal packet_size      : natural := 256;
    constant Attr_Fifo_Base         : unsigned(19 downto 0) := X"00700"; -- 380 * 2 
    constant Attr_Fifo_Tail_Address : unsigned(19 downto 0) := X"007F0"; -- 3f8 * 2
    constant Attr_Fifo_Head_Address : unsigned(19 downto 0) := X"007F2"; -- 3f9 * 2
begin
    i_harness: entity work.usb_harness_nano
    port map (
        ack_on_ping    => ack_on_ping,
        nyet_count     => nyet_count,
        interrupt      => interrupt,
        transfer_size  => transfer_size,
        packet_size    => packet_size,
        clocks_stopped => clocks_stopped );

    process
        variable io : p_io_bus_bfm_object;
        variable mem : h_mem_object;
        variable data, remain, newcmd : std_logic_vector(15 downto 0);
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

        procedure read_attr_fifo(result : out std_logic_vector(15 downto 0); timeout : time; new_cmd, remain : out std_logic_vector(15 downto 0)) is
            variable data   : std_logic_vector(15 downto 0);
            variable cmd    : std_logic_vector(15 downto 0);
            variable res    : std_logic_vector(15 downto 0);
            variable len    : std_logic_vector(15 downto 0);
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
            io_read_word(addr => Command_Length, word => len);
            sctb_trace("Fifo read: " & hstr(data) & ", Command: " & hstr(cmd) & ", Result: " & hstr(res) & ", Remaining: " & hstr(len));
            result := res;
            new_cmd := cmd;
            remain := len;
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

        sctb_open_region("Testing an in packet in high speed, transfer size = packet_size", 0);

        transfer_size <= 16;
        packet_size <= 16;

        write_memory_8(mem, X"00050000", X"11");        
        write_memory_8(mem, X"00050001", X"22");        
        write_memory_8(mem, X"00050002", X"33");        
        write_memory_8(mem, X"00050003", X"44");        
        write_memory_8(mem, X"00050004", X"55");        
        write_memory_8(mem, X"00050005", X"66");        
        write_memory_8(mem, X"00050006", X"77");        
        write_memory_8(mem, X"00050007", X"88");        

        io_write_word(Command_SplitCtl, X"8131"); -- Hub Address 1, Port 2, Speed = FS, EP = control 
        --io_write_word(Command_SplitCtl, X"0000"); -- High speed 
        io_write_word(Command_DevEP,    X"0004");
        io_write_word(Command_MaxTrans, X"0010");
        io_write_word(Command_Length,   X"0010");
        io_write_word(Command_MemHi,    X"0005");
        io_write_word(Command_MemLo,    X"0000");
        io_write_word(Command_Interval, X"0000");
        io_write_word(Command_Timeout,  X"001A");
        io_write_word(c_nano_numpipes,   X"0001"  ); -- Set active pipes to 1

        --io_write_word(Command, X"8041"); -- output
        io_write_word(Command, X"4042"); -- 4000 = write to memory, 40 = Do Data, 02 = IN

        read_attr_fifo(data, 1.5 ms, newcmd, remain);
        sctb_check(data, X"8010", "Unexpected result. Expected data packet of 16 bytes.");
        sctb_check(remain, X"0000", "Expected all data to be transferred.");
        sctb_check(newcmd, X"4A42", "Expected toggle bit to be set.");
        read_attr_fifo(data, 1.5 ms, newcmd, remain);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;


        sctb_open_region("Testing an in packet in high speed, transfer size < packet_size. Note, we start with DATA 1!", 0);

        --io_write_word(Command_SplitCtl, X"8130"); -- Hub Address 1, Port 2, Speed = FS, EP = control 
        transfer_size <= 8;
        packet_size <= 16;

        io_write_word(Command_MaxTrans, X"0008");
        io_write_word(Command_Length,   X"0011");
        io_write_word(Command_Started, X"0000");
        io_write_word(Command, X"4842"); -- 4000 = write to memory, 40 = Do Data, 02 = IN, 800 = toggle is 1


        read_attr_fifo(data, 2.5 ms, newcmd, remain);
        sctb_check(data and X"F7FF", X"8400", "Unexpected result. Expected data packet of 0 bytes in the last transfer.");
        sctb_check(remain, X"0001", "Expected one data byte to be left.");
        sctb_check(newcmd, X"4242", "Expected toggle bit to be cleared.");
        read_attr_fifo(data, 1.5 ms, newcmd, remain);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;


        sctb_open_region("Testing an in packet in high speed, transfer size > packet_size. Note, we start with DATA 0!", 0);

        --io_write_word(Command_SplitCtl, X"8130"); -- Hub Address 1, Port 2, Speed = FS, EP = control 
        transfer_size <= 32;
        packet_size <= 22;

        io_write_word(Command_MaxTrans, X"0020");
        io_write_word(Command_Length,   X"0020");
        io_write_word(Command_Started, X"0000");
        io_write_word(Command, X"4042"); -- 4000 = write to memory, 40 = Do Data, 02 = IN, 


        read_attr_fifo(data, 1.5 ms, newcmd, remain);
        sctb_check(data and X"F7FF", X"8016", "Unexpected result. Expected data packet of 16 bytes in the last transfer.");
        sctb_check(remain, X"000A", "Expected 10 bytes to be left.");
        sctb_check(newcmd, X"4A42", "Expected toggle bit to be set.");
        read_attr_fifo(data, 1.5 ms, newcmd, remain);
        sctb_check(data, X"FFFF", "Result should be TIMEOUT, as the core should have stopped trying after the last attempt");
        sctb_close_region;


        sctb_close_simulation;
        clocks_stopped <= true;
        
        wait;
    end process;
        
end arch;

-- restart; mem load -infile nano_code.hex -format hex /usb_test_nano7/i_harness/i_host/i_nano/i_buf_ram/mem; run 20 ms