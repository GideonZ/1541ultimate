--------------------------------------------------------------------------------
-- Entity: acia6551
-- Date:2018-11-24  
-- Author: gideon     
--
-- Description: This is the testbench for the 6551.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.slot_bus_pkg.all;
use work.slot_bus_master_bfm_pkg.all;
use work.acia6551_pkg.all;
use work.tl_string_util_pkg.all;

entity tb_acia6551 is

end entity;

architecture testbench of tb_acia6551 is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal slot_req        : t_slot_req;
    signal slot_resp       : t_slot_resp;
    signal io_req          : t_io_req;
    signal io_resp         : t_io_resp;
    signal io_irq          : std_logic;

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_acia: entity work.acia6551
    port map (
        clock     => clock,
        reset     => reset,
        slot_req  => slot_req,
        slot_resp => slot_resp,
        io_req    => io_req,
        io_resp   => io_resp,
        io_irq    => io_irq
    );

    i_io_master: entity work.io_bus_bfm
    generic map (
        g_name => "io"
    )
    port map (
        clock  => clock,
        req    => io_req,
        resp   => io_resp
    );
    
    i_slot_master: entity work.slot_bus_master_bfm
    generic map (
        g_name => "slot"
    )
    port map (
        clock  => clock,
        req    => slot_req,
        resp   => slot_resp
    );
    
    p_test: process
        variable io     : p_io_bus_bfm_object;
        variable slot   : p_slot_bus_master_bfm_object;
        variable data   : std_logic_vector(7 downto 0);
        variable status : std_logic_vector(7 downto 0);

        procedure read_status_and_data is
        begin
            slot_io_read(slot, c_addr_status_register, status);
            slot_io_read(slot, c_addr_data_register, data);
            report hstr(status) & ":" & hstr(data);
        end procedure;

        procedure read_tx is
            variable head, tail : std_logic_vector(7 downto 0);
        begin
            io_read(io, c_reg_tx_head, head);
            io_read(io, c_reg_tx_tail, tail);
            io_read(io, unsigned(tail) + X"800", data);
            report hstr(head) & ":" & hstr(tail) & ":" & hstr(data);
            if head /= tail then
                io_write(io, c_reg_tx_tail, std_logic_vector(unsigned(tail) + 1));
            end if;
        end procedure;
    begin
        bind_io_bus_bfm("io", io);
        bind_slot_bus_master_bfm("slot", slot);
        wait until reset = '0';
        io_write(io, c_reg_enable, X"01" );
        io_write(io, X"900", X"47");
        io_write(io, X"901", X"69");
        io_write(io, X"902", X"64");
        io_write(io, X"903", X"65");
        io_write(io, c_reg_rx_head, X"04" );

        read_status_and_data;

        -- Set DTR
        slot_io_write(slot, c_addr_command_register, X"03");
        
        read_status_and_data;
        read_status_and_data;
        read_status_and_data;
        read_status_and_data;

        io_write(io, X"904", X"6f");
        io_write(io, X"905", X"6e");
        io_write(io, c_reg_rx_head, X"06" );

        read_status_and_data;
        read_status_and_data;
        read_status_and_data;

        -- Now the other way around
        slot_io_write(slot, c_addr_data_register, X"01");
        slot_io_write(slot, c_addr_data_register, X"02");
        slot_io_write(slot, c_addr_data_register, X"03");
        slot_io_write(slot, c_addr_data_register, X"04");

        read_tx;
        read_tx;
        read_tx;
        read_tx;
        read_tx;

        for i in 0 to 260 loop
            slot_io_write(slot, c_addr_data_register, std_logic_vector(to_unsigned((i + 6) mod 256, 8)));
        end loop;    

        read_tx;
        read_tx;
        read_tx;
        read_tx;
        read_tx;
        wait;
    end process;
end architecture;
