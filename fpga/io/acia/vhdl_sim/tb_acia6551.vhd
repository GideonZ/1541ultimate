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
use work.strings_pkg.all;

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
    signal tick            : std_logic;
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    process -- a tick rate of 100 ns is 10 MHz. Observed rates will be 2.5 times faster than programmed
    begin
        tick <= '0';
        wait for 80 ns;
        tick <= '1';
        wait for 20 ns;
    end process;    

    i_acia: entity work.acia6551
    port map (
        clock     => clock,
        reset     => reset,
        slot_req  => slot_req,
        slot_resp => slot_resp,
        tick      => tick,
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
            L1: while true loop
                slot_io_read(slot, c_addr_status_register, status);
                if not exp or status(3) = '1' then
                    exit L1;
                end if;
            end loop; 
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
                assert not exp report "There is no data, but you did expect some! " & hstr(expdata) severity error;
            end if;
        end procedure;

        procedure tx_byte_polling(d : std_logic_vector(7 downto 0)) is
        begin
            L1: while true loop
                slot_io_read(slot, c_addr_status_register, data);
                if data(4) = '1' then
                    exit L1;
                end if;
            end loop;
            slot_io_write(slot, c_addr_data_register, d);
        end procedure;

        variable st : time;
        variable nsec : natural;
    begin
        bind_io_bus_bfm("io", io);
        bind_slot_bus_master_bfm("slot", slot);
        wait until reset = '0';

        -- Section 1: Read from ACIA in poll loop
        check_io_irq('0');
        
        -- Enable and pass some bytes to the RX of the Host
        io_write(io, c_reg_enable, X"19" ); -- enable IRQ on DTR change and control write

        io_write(io, X"A00", X"47");
        io_write(io, X"A01", X"69");
        io_write(io, X"A02", X"64");
        io_write(io, X"A03", X"65");
        io_write(io, X"A04", X"6F");
        io_write(io, X"A05", X"6E");
        io_write(io, c_reg_rx_head, X"06" );

        -- On the host side, read status. It should show no data, since DTR is not set
        read_status_and_data;

        -- Set DTR
        slot_io_write(slot, c_addr_command_register, X"03");

        -- set the virtual baud rate
        slot_io_write(slot, c_addr_control_register, X"1F"); -- 19200 baud (48000 with 5 MHz tick)
        -- read back control register
        io_read(io, c_reg_control, status);
        assert status = X"1F" report "Control read register is different from what we wrote earlier." severity error;

        -- control and handshake have both changed
        check_io_irq('1', X"1A");  -- Checks and clears IRQ
        check_io_irq('0');

        -- Now that DTR is set, reading the host status should show that there is data available
        st := now;
        read_status_and_data(true, X"47");
        read_status_and_data(true, X"69");
        read_status_and_data(true, X"64");
        read_status_and_data(true, X"65");
        read_status_and_data(true, X"6F");
        read_status_and_data(true, X"6E");
        report "Read 6 bytes in " & time'image(now - st);
        read_status_and_data;

        -- Section 2: Read from ACIA in interrupt
        slot_io_write(slot, c_addr_command_register, X"01"); -- disable Rx IRQ off
        io_write(io, X"A06", X"47");
        io_write(io, X"A07", X"69");
        io_write(io, X"A08", X"64");
        io_write(io, X"A09", X"65");
        io_write(io, X"A0A", X"6F");
        io_write(io, X"A0B", X"6E");
        io_write(io, c_reg_rx_head, X"0C" );
        st := now;
        wait until slot_resp.nmi = '1';
        read_status_and_data(true, X"47");
        wait until slot_resp.nmi = '1';
        read_status_and_data(true, X"69");
        wait until slot_resp.nmi = '1';
        read_status_and_data(true, X"64");
        wait until slot_resp.nmi = '1';
        read_status_and_data(true, X"65");
        wait until slot_resp.nmi = '1';
        read_status_and_data(true, X"6F");
        wait until slot_resp.nmi = '1';
        read_status_and_data(true, X"6E");
        report "Read 6 bytes in " & time'image(now - st);
        st := now - st;
        nsec := (st / 1 ns) / 6; -- ns per byte
        nsec := (nsec * 5) / 20; -- factor 2.5, but 10 bits / byte
        report "Bit rate: " & integer'image(1_000_000_000 / nsec);
        read_status_and_data;

        -- Section 3: Transmit data without interrupt

        io_write(io, c_reg_enable, X"05"); -- enable Tx Interrupt
        assert io_irq = '0' report "IO IRQ should be zero now." severity error;

        io_write(io, c_reg_handsh, X"01"); -- Set CTS

        -- Now the other way around  (from Host to Appl)
        tx_byte_polling(X"01");
        tx_byte_polling(X"02");
        tx_byte_polling(X"03");
        tx_byte_polling(X"04");

        wait for 250 us; -- byte should be transmitted in full

        assert io_irq = '1' report "IO IRQ should be active now." severity error;

        read_tx(true, X"01");
        read_tx(true, X"02");
        read_tx(true, X"03");
        read_tx(true, X"04");
        read_tx;

        assert io_irq = '0' report "IO IRQ should be zero now." severity error;

        -- Section 4: Transmit data with interrupt
        slot_io_write(slot, c_addr_command_register, X"07"); -- disable Rx IRQ, but enable Tx IRQ

        -- Start transmission by writing a data byte (We know that the tx_empty is 1)
        slot_io_write(slot, c_addr_data_register, X"11");
        wait until slot_resp.nmi = '1';
        slot_io_read(slot, c_addr_status_register, data); -- should clear nmi, and show that tx empty is true
        wait until clock = '1';
        wait until clock = '1';
        assert data(4) = '1' report "TxEmpty should be true." severity error;
        assert slot_resp.nmi = '0' report "NMI should now be inactive!" severity error;
        slot_io_write(slot, c_addr_data_register, X"12");
        wait until slot_resp.nmi = '1';
        slot_io_read(slot, c_addr_status_register, data); -- should clear nmi, and show that tx empty is true
        wait until clock = '1';
        wait until clock = '1';
        assert data(4) = '1' report "TxEmpty should be true." severity error;
        assert slot_resp.nmi = '0' report "NMI should now be inactive!" severity error;
        slot_io_write(slot, c_addr_data_register, X"13");
        wait until slot_resp.nmi = '1';
        slot_io_read(slot, c_addr_status_register, data); -- should clear nmi, and show that tx empty is true
        wait until clock = '1';
        wait until clock = '1';
        assert data(4) = '1' report "TxEmpty should be true." severity error;
        assert slot_resp.nmi = '0' report "NMI should now be inactive!" severity error;
        slot_io_write(slot, c_addr_data_register, X"14");
        wait until slot_resp.nmi = '1';
        slot_io_read(slot, c_addr_status_register, data); -- should clear nmi, and show that tx empty is true
        wait until clock = '1';
        wait until clock = '1';
        assert data(4) = '1' report "TxEmpty should be true." severity error;
        assert slot_resp.nmi = '0' report "NMI should now be inactive!" severity error;
        slot_io_write(slot, c_addr_command_register, X"03"); -- disable Tx IRQ again

        wait for 250 us; -- byte should be transmitted in full

        read_tx(true, X"11");
        read_tx(true, X"12");
        read_tx(true, X"13");
        read_tx(true, X"14");
        wait;
    end process;
end architecture;
