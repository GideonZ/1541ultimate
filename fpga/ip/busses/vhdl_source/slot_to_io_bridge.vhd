-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2012 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Bus bridge between C64 Slot cycles and internal I/O bus.
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements the bus-bridge between the C64 I/O
--              expansion and the internal I/O bus to the peripherals.
--              Due to the nature of the I/O bus, reads are not yet implemented.
--              In order to make this work an io_bus read shall be initiated
--              when a read takes place, but before the end of the C64 cycle.
--              The necessary signal is not defined yet in the slot_bus
--              definition.
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.slot_bus_pkg.all;

entity slot_to_io_bridge is
generic (
    g_io_base       : unsigned(19 downto 0);
    g_slot_start    : unsigned(8 downto 0);
    g_slot_stop     : unsigned(8 downto 0) );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    enable          : in  std_logic := '1';
    irq_in          : in  std_logic := '0';
    
    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp;
    
    io_req          : out t_io_req;
    io_resp         : in  t_io_resp );

end entity;


architecture rtl of slot_to_io_bridge is
    signal last_read    : std_logic_vector(7 downto 0);
    signal reg_output   : std_logic;
begin
    slot_resp.irq        <= irq_in and enable;
    slot_resp.data       <= last_read when reg_output='1' else X"00";
    
    process(clock)
        variable addr   : unsigned(8 downto 0);
    begin
        if rising_edge(clock) then
            io_req <= c_io_req_init;

            -- writes
            addr := slot_req.io_address(8 downto 0);
            if (addr >= g_slot_start) and (addr <= g_slot_stop) and (enable = '1') then
                if slot_req.io_write='1' then
                    io_req.write   <= '1';
                    io_req.address <= g_io_base + (addr - g_slot_start);
                    io_req.data    <= slot_req.data;
                end if;
            end if;
            -- reads
            addr := slot_req.bus_address(8 downto 0);
            if (addr >= g_slot_start) and (addr <= g_slot_stop) and (enable = '1') then
                if slot_req.io_read_early='1' then
                    io_req.read    <= '1';
                    io_req.address <= g_io_base + (addr - g_slot_start);
                    last_read      <= X"AA";
                end if;
            end if;
            if io_resp.ack = '1' then
                last_read <= io_resp.data;
            end if;
        end if;
    end process;

    -- reads
    reg_output <= enable when (slot_req.bus_address(8 downto 0) >= g_slot_start) and
                              (slot_req.bus_address(8 downto 0) <= g_slot_stop) else '0';

    slot_resp.reg_output <= reg_output;
    
end architecture;
