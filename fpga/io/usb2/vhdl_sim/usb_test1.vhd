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

entity usb_test1 is

end entity;

architecture arch of usb_test1 is
    signal clocks_stopped   : boolean := false;
begin
    i_harness: entity work.usb_harness
    port map (
        clocks_stopped => clocks_stopped );

    process
        variable io : p_io_bus_bfm_object;
        variable data : std_logic_vector(15 downto 0);
    begin
        bind_io_bus_bfm("io", io);
        sctb_open_simulation("path:path", "usb_test1.tcr");
        sctb_open_region("Testing In request", 0);
        sctb_set_log_level(c_log_level_trace);
        wait for 70 ns;
        io_write(io => io, addr => X"017fe", data => X"01"  ); -- set nano to simulation mode
        io_write(io => io, addr => X"017fc", data => X"02"  ); -- set bus speed to HS
        io_write(io => io, addr => X"01800", data => X"01"  ); -- enable nano
        wait for 4 us;

        io_write(io => io, addr => X"00006", data => X"00"  ); -- data buffer control BufIdx(2) ND TOG - - len(2) 
        io_write(io => io, addr => X"00007", data => X"00"  ); -- data buffer control Len(8)

        io_write(io => io, addr => X"0000A", data => X"01"  ); -- DEV=1
        io_write(io => io, addr => X"0000B", data => X"04"  ); -- EP=4
        io_write(io => io, addr => X"00001", data => X"42"  ); -- DoSplit=0, DoData=1, Cmd=2 (setup, out_data, in_request, ping, bus_reset)

        L1: while true loop
            io_read(io => io, addr => X"00000", data => data(15 downto 8) );
            if data(15) = '1' then
                exit L1;
            end if;
        end loop;
        io_read(io => io, addr => X"00001", data => data(7 downto 0) );
        sctb_trace("Command result: " & t_usb_result'image(t_usb_result'val(to_integer(unsigned(data(14 downto 12))))) &
                    ", length: " & integer'image(to_integer(unsigned(data(9 downto 0)))) );

        io_write(io => io, addr => X"00006", data => X"01"  ); -- data buffer control BufIdx(2) ND TOG - - len(2) 
        io_write(io => io, addr => X"00007", data => X"00"  ); -- data buffer control Len(8)
        io_write(io => io, addr => X"0000B", data => X"01"  ); -- EP=1
        io_write(io => io, addr => X"00001", data => X"41"  ); -- DoSplit=0, DoData=1, Cmd=1 (setup, out_data, in_request, ping, bus_reset)

        L2: while true loop
            io_read(io => io, addr => X"00000", data => data(15 downto 8) );
            if data(15) = '1' then
                exit L2;
            end if;
        end loop;
        io_read(io => io, addr => X"00001", data => data(7 downto 0) );
        sctb_trace("Command result: " & t_usb_result'image(t_usb_result'val(to_integer(unsigned(data(14 downto 12))))) &
                    ", length: " & integer'image(to_integer(unsigned(data(9 downto 0)))) );
        wait;
    end process;
        
end arch;

