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
        g_report_file_name : string := "work/usb_test_nano6.rpt"
    );
end entity;

architecture arch of usb_test_nano7 is
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

        procedure read_attr_fifo(result : out std_logic_vector(15 downto 0)) is
            variable data   : std_logic_vector(15 downto 0);
        begin
            wait until interrupt = '1';
            io_read_word(addr => (Attr_Fifo_Base + attr_fifo_tail*2), word => data);
            attr_fifo_tail := attr_fifo_tail + 1;
            if attr_fifo_tail = 16 then
                attr_fifo_tail := 0;
            end if;
            io_write_word(addr => Attr_Fifo_Tail_Address, word => std_logic_vector(to_unsigned(attr_fifo_tail, 16)));
            sctb_trace("Fifo read: " & hstr(data));
            result := data;
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

        sctb_open_region("Testing 2 in requests at the same time, using split with different intervals", 0);

        io_write_word(Command_SplitCtl, X"8132"); -- Hub Address 1, Port 2, Speed = FS, EP = interrupt 
        io_write_word(Command_DevEP,    X"0005");
        io_write_word(Command_MaxTrans, X"0010");
        io_write_word(Command_Length,   X"0010");
        io_write_word(Command_MemHi,    X"0005");
        io_write_word(Command_MemLo,    X"0000");
        io_write_word(Command_Interval, X"0015");

        io_write_word(c_nano_numpipes,   X"0002"  ); -- Set active pipes to 2

        io_write_word(Command_SplitCtl + Command_Size, X"8132"); -- Hub Address 1, Port 2, Speed = FS, EP = interrupt 
        io_write_word(Command_DevEP + Command_Size,    X"0008");
        io_write_word(Command_MaxTrans + Command_Size, X"0010");
        io_write_word(Command_Length + Command_Size,   X"0010");
        io_write_word(Command_MemHi + Command_Size,    X"0010");
        io_write_word(Command_MemLo + Command_Size,    X"0000");
        io_write_word(Command_Interval + Command_Size, X"0014");

        io_write_word(Command + Command_Size, X"4042");
        io_write_word(Command, X"4042"); 

        for i in 1 to 10 loop
            read_attr_fifo(data);
            pipe := Command_size * to_integer(unsigned(data));
            io_read_word(Command + pipe, data);
            data(9) := '0'; -- unpause, but leave toggle bits in tact
            io_write_word(Command + pipe, data); 
        end loop;

        pipe := Command_size * to_integer(unsigned(data));
        io_read_word(Command_Result + pipe, data);
        sctb_trace("Command result: " & hstr(data));
        io_read_word(Command_Length + pipe, data);
        sctb_trace("Command length: " & hstr(data));
        sctb_check(data, X"0000", "All data should have been transferred.");

        read_attr_fifo(data);
        pipe := Command_size * to_integer(unsigned(data));
        io_read_word(Command_Result + pipe, data);
        sctb_trace("Command result: " & hstr(data));
        io_read_word(Command_Length + pipe, data);
        sctb_trace("Command length: " & hstr(data));
        sctb_check(data, X"0000", "All data should have been transferred.");

        sctb_close_region;

        sctb_close_simulation;
        clocks_stopped <= true;
        
        wait;
    end process;
        
end arch;

-- restart; mem load -infile nano_code.hex -format hex /usb_test_nano7/i_harness/i_host/i_nano/i_buf_ram/mem; run 20 ms