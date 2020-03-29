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

        procedure check_io_irq(level : std_logic; flags : std_logic_vector(7 downto 0) := X"00") is
            variable reg : std_logic_vector(7 downto 0);
        begin
            wait until clock = '1';
            wait until clock = '1';
            wait until clock = '1';
            assert level = io_irq report "Level of IO_irq is wrong." severity error;
            if level = '1' then
                io_read(io, c_reg_irq_source, reg);
                assert flags = reg report "IRQ flags unexpected. " & hstr(reg) & "/=" & hstr(flags)
                    severity error;
                io_write(io, c_reg_irq_source, flags);
            end if;
        end procedure;
        
        procedure read_status_and_data(exp: boolean := false; expdata : std_logic_vector(7 downto 0) := X"00") is
        begin
            slot_io_read(slot, c_addr_status_register, status);
            slot_io_read(slot, c_addr_data_register, data);
            report hstr(status) & ":" & hstr(data);
            if exp and status(3) = '0' then
                report "Rx Data expected, but not available."
                severity error;
            end if;
            if not exp and status(3) = '1' then
                report "NO data expected, but there is Rx data available."
                severity error;
            end if;
            if exp and status(3) = '1' and data /= expdata then
                report "Wrong Rx data read."
                severity error;
            end if;
        end procedure;

        procedure read_tx(exp: boolean := false; expdata : std_logic_vector(7 downto 0) := X"00") is
            variable head, tail : std_logic_vector(7 downto 0);
        begin
            io_read(io, c_reg_tx_head, head);
            io_read(io, c_reg_tx_tail, tail);
            io_read(io, unsigned(tail) + X"800", data);
            report hstr(head) & ":" & hstr(tail) & ":" & hstr(data);
            if head /= tail then
                io_write(io, c_reg_tx_tail, std_logic_vector(unsigned(tail) + 1));
                assert exp report "There is data, but you didn't expected any." severity error;
                assert data = expdata report "Data is not as expected?" severity error;
            else
                assert not exp report "There is no data, but you did expect some!" severity error;
            end if;
        end procedure;
    begin
        bind_io_bus_bfm("io", io);
        bind_slot_bus_master_bfm("slot", slot);
        wait until reset = '0';

        check_io_irq('0');
        
        -- Enable and pass four bytes to the RX of the Host
        io_write(io, c_reg_enable, X"19" ); -- enable IRQ on DTR change and control write
        io_write(io, X"900", X"47");
        io_write(io, X"901", X"69");
        io_write(io, X"902", X"64");
        io_write(io, X"903", X"65");
        io_write(io, c_reg_rx_head, X"04" );

        -- On the host side, read status. It should show no data, since DTR is not set
        read_status_and_data;

        -- Set DTR
        slot_io_write(slot, c_addr_command_register, X"03");
        check_io_irq('1', X"12");  -- Checks and clears IRQ
        check_io_irq('0');
        
        -- set the virtual baud rate
        slot_io_write(slot, c_addr_control_register, X"1A");
        check_io_irq('1', X"0A");  -- Checks and clears IRQ
        check_io_irq('0');
        io_read(io, c_reg_control, status);
        assert status = X"1A" report "Control read register is different from what we wrote earlier." severity error;
                
        -- Now that DTR is set, reading the host status should show that there is data available
        read_status_and_data(true, X"47");
        read_status_and_data(true, X"69");
        read_status_and_data(true, X"64");
        read_status_and_data(true, X"65");
        read_status_and_data;

        io_write(io, X"904", X"6f");
        io_write(io, X"905", X"6e");
        io_write(io, c_reg_rx_head, X"06" );

        read_status_and_data(true, X"6F");
        read_status_and_data(true, X"6E");
        read_status_and_data;

        io_write(io, c_reg_enable, X"05"); -- enable Tx Interrupt
        assert io_irq = '0' report "IO IRQ should be zero now." severity error;

        -- Now the other way around  (from Host to Appl)
        slot_io_write(slot, c_addr_data_register, X"01");
        slot_io_write(slot, c_addr_data_register, X"02");
        slot_io_write(slot, c_addr_data_register, X"03");
        slot_io_write(slot, c_addr_data_register, X"04");

        assert io_irq = '1' report "IO IRQ should be active now." severity error;

        read_tx(true, X"01");
        read_tx(true, X"02");
        read_tx(true, X"03");
        read_tx(true, X"04");
        read_tx;

        for i in 0 to 260 loop
            slot_io_write(slot, c_addr_data_register, std_logic_vector(to_unsigned((i + 6) mod 256, 8)));
        end loop;    

        read_tx(true, X"06");
        read_tx(true, X"07");
        read_tx(true, X"08");
        read_tx(true, X"09");
        read_tx(true, X"0A");
        wait;
    end process;
end architecture;
