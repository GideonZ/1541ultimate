--------------------------------------------------------------------------------
-- Entity: acia6551
-- Date:2018-11-13  
-- Author: gideon     
--
-- Description: This is a simple implementation of the 6551.
--              It does not actually have a serial port, but just behaves like
--              it does.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;
use work.slot_bus_pkg.all;
use work.acia6551_pkg.all;

entity acia6551 is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    c64_reset       : in  std_logic := '0';
    tick            : in  std_logic;
    
    -- C64 side interface
    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp;
        
    -- io interface for local cpu
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq          : out std_logic );

end entity;

architecture arch of acia6551 is
    type t_rate_array is array(natural range <>) of integer;
    constant c_rates : t_rate_array(0 to 15) := (28800, 50, 75, 110, 135, 150, 300, 600, 1200, 1800, 2400, 3600, 4800, 7200, 9600, 19200);    

    function rate_dividers(freq : natural) return t_rate_array is
        variable ret : t_rate_array(0 to 15);
    begin
        for i in c_rates'range loop
            ret(i) := ((10 * freq) / (7 * c_rates(i))) - 1; -- Assuming 8 'ticks' per byte, in order to use the same divider for Rx and Tx, and 10 bits per byte
        end loop;
        return ret;
    end function;
    constant c_rate_dividers    : t_rate_array(0 to 15) := rate_dividers(4_000_000);

    signal rate_counter     : integer range 0 to 131071; -- only 5 chars per second!
    signal rate_tick        : std_logic; -- 8x per byte
    signal rx_presc         : unsigned(6 downto 0) := "0000000";
    signal tx_presc         : unsigned(2 downto 0) := "000";

    signal slot_base        : unsigned(8 downto 3) := (others => '0');
    signal rx_data          : std_logic_vector(7 downto 0) := X"FF";
    signal status           : std_logic_vector(7 downto 0) := X"00";
    signal command          : std_logic_vector(7 downto 0);
    signal control          : std_logic_vector(7 downto 0);

    signal tx_data          : std_logic_vector(7 downto 0);
    signal tx_fifo_full     : std_logic;

    signal dsr_d            : std_logic;
    signal dcd_d            : std_logic;
    alias irq               : std_logic is status(7);
    alias dsr_n             : std_logic is status(6);
    alias dcd_n             : std_logic is status(5);
    alias tx_empty          : std_logic is status(4);
    alias rx_full           : std_logic is status(3);
    alias overrun_err       : std_logic is status(2);
    alias framing_err       : std_logic is status(1);
    alias parity_err        : std_logic is status(0);

    alias baud_index        : std_logic_vector(3 downto 0) is control(3 downto 0);

    alias dtr               : std_logic is command(0);
    alias rx_irq_disable    : std_logic is command(1);
    alias tx_mode           : std_logic_vector(1 downto 0) is command(3 downto 2);
    signal dtr_d            : std_logic;
    
    signal enable           : std_logic;
    signal nmi_selected     : std_logic;
    signal tx_irq_en        : std_logic;
    signal rx_irq_en        : std_logic;
    signal soft_reset       : std_logic;
    signal appl_tx_irq      : std_logic := '0';
    signal appl_rx_irq      : std_logic := '0';
    signal ctrl_irq_en      : std_logic;
    signal hs_irq_en        : std_logic;
    signal control_change   : std_logic;
    signal dtr_change       : std_logic;
    signal rx_pushback      : std_logic;
    
    signal cts              : std_logic; -- written by sys
    signal rts              : std_logic; -- written by slot (command register)

    signal rx_head, rx_tail : unsigned(7 downto 0);
    signal tx_head, tx_tail : unsigned(7 downto 0);

    signal b_address        : unsigned(8 downto 0);
    signal b_rdata          : std_logic_vector(7 downto 0);
    signal b_wdata          : std_logic_vector(7 downto 0);
    signal b_en, b_we       : std_logic;
    signal b_pending        : std_logic;

    signal io_req_regs      : t_io_req;
    signal io_resp_regs     : t_io_resp := c_io_resp_init;
    signal io_req_ram       : t_io_req;
    signal io_resp_ram      : t_io_resp := c_io_resp_init;
    signal io_ram_ack       : std_logic;
    signal io_ram_en        : std_logic;
    signal io_ram_rdata     : std_logic_vector(7 downto 0);
    signal turbo_en         : std_logic;
    signal turbo_sp         : std_logic_vector(1 downto 0) := "00";
    signal turbo_rb         : std_logic_vector(7 downto 0) := X"00";
begin
    with slot_req.bus_address(2 downto 0) select slot_resp.data <=
        rx_data     when c_addr_data_register,
        status      when c_addr_status_register,
        command     when c_addr_command_register, 
        control     when c_addr_control_register,
        turbo_rb    when c_addr_turbo_register,
        X"FF"       when others;   

    turbo_rb(2) <= '1' when baud_index = "0000" else '0';
    turbo_rb(1 downto 0) <= turbo_sp;

    -- ena tur ad2 | reg
    --  0   0   0  |  0 <- disabled
    --  0   0   1  |  0 <- disabled
    --  0   1   0  |  0 <- disabled
    --  0   1   1  |  0 <- disabled
    --  1   0   0  |  1 <- first 4 bytes
    --  1   0   1  |  0 <- second 4 bytes with turbo off does not respond
    --  1   1   0  |  1 <- first 4 bytes
    --  1   1   1  |  1 <- second 4 bytes (8 bytes when turbo_en = 1)
 
    slot_resp.reg_output <= enable and (turbo_en or not slot_req.bus_address(2)) when slot_req.bus_address(8 downto 3) = slot_base else '0';
    slot_resp.irq  <= enable and irq and not nmi_selected;
    slot_resp.nmi  <= enable and irq and nmi_selected;

    rts       <= '0' when tx_mode = "00" else '1';

    -- IRQs to the Application (IO side)
    appl_rx_irq  <= '0' when (rx_head + 1) = rx_tail else '1'; -- RX = Appl -> Host (room for data appl can write)
    appl_tx_irq  <= '1' when tx_head /= tx_tail else '0';      -- TX = Host -> Appl (data appl should read)
    io_irq       <= (appl_rx_irq and rx_irq_en) or (appl_tx_irq and tx_irq_en) or
                    (control_change and ctrl_irq_en) or (dtr_change and hs_irq_en);
    
    process(clock)
    begin
        if rising_edge(clock) then
            soft_reset <= '0';
            
            if tx_head + 1 = tx_tail then
                tx_fifo_full <= '1';
            else
                tx_fifo_full <= '0';
            end if;

            rate_tick <= '0';
            if tick = '1' then
                if rate_counter = 0 or rate_counter = 1 then -- Allow counting by steps of 2
                    rate_tick <= '1';
                    rate_counter <= c_rate_dividers(to_integer(unsigned(baud_index)));
                else
                    rate_counter <= rate_counter - 2;
                end if;
            end if;

            if dtr = '0' then
                rx_presc <= "1111111";
            elsif rate_tick = '1' and rx_presc /= 0 then
                rx_presc <= rx_presc - 1;
            end if;

            if rate_tick = '1' and tx_presc /= 0 then
                tx_presc <= tx_presc - 1;
            end if;

            b_en <= '0';
            b_we <= '0';
            b_address <= (others => 'X');
            b_wdata <= (others => 'X');

            -- BlockRAM access
            -- For transmit, CTS should be active
            if tx_empty = '0' and cts = '1' and tx_fifo_full = '0' and tx_presc = "000" then
                b_address <= '0' & tx_head;
                b_wdata <= tx_data;
                b_we <= '1';
                b_en <= '1';
                tx_head <= tx_head + 1;
                tx_presc <= "111";
                tx_empty <= '1';
                if tx_mode = "01" then
                    irq <= '1';
                end if;
            elsif rx_head /= rx_tail and b_pending = '0' then -- there is data to be received
                if (rx_presc <= "1111000" and rx_full = '0') or (rx_presc = 0) then 
                    b_address <= '1' & rx_tail;
                    b_en <= '1';
                    b_pending <= '1';
                    rx_tail <= rx_tail + 1;
                    rx_presc <= "1111111";
                end if;
            end if;

            if (slot_req.io_address(8 downto 3) = slot_base) and (enable = '1') then
                if slot_req.io_write='1' then
                    case slot_req.io_address(2 downto 0) is
                    when c_addr_data_register =>
                        tx_data <= slot_req.data;
                        tx_empty <= '0';
                    when c_addr_status_register =>
                        soft_reset <= '1';
                    when c_addr_command_register =>
                        command <= slot_req.data;
                    when c_addr_control_register =>
                        control <= slot_req.data;
                        control_change <= '1';
                    when c_addr_turbo_register =>
                        if turbo_en = '1' and turbo_rb(2) = '1' then
                            turbo_sp <= slot_req.data(1 downto 0);
                        end if;
                    when others =>
                        null;
                    end case;
                elsif slot_req.io_read='1' then
                    case slot_req.io_address(2 downto 0) is
                    when c_addr_data_register =>
                        parity_err <= '0';
                        framing_err <= '0';
                        overrun_err <= '0';
                        rx_full <= '0';
                    when c_addr_status_register =>
                        irq <= '0';
                    when c_addr_command_register =>
                        null;
                    when c_addr_control_register =>
                        null;
                    when others =>
                        null;
                    end case;
                end if;
            end if;

            io_resp_regs <= c_io_resp_init;
            if io_req_regs.write='1' then
                io_resp_regs.ack <= '1';
                case io_req_regs.address(3 downto 0) is
                when c_reg_rx_head =>
                    rx_head <= unsigned(io_req_regs.data);
                when c_reg_tx_tail =>
                    tx_tail <= unsigned(io_req_regs.data);
                when c_reg_enable =>
                    enable <= io_req_regs.data(0);
                    rx_irq_en <= io_req_regs.data(1);
                    tx_irq_en <= io_req_regs.data(2);
                    ctrl_irq_en <= io_req_regs.data(3);
                    hs_irq_en <= io_req_regs.data(4);
                when c_reg_handsh =>
                    cts   <= io_req_regs.data(0);
                    dsr_n <= not io_req_regs.data(2);
                    dcd_n <= not io_req_regs.data(4);
                    rx_pushback <= io_req_regs.data(6);
                when c_reg_irq_source =>
                    if io_req_regs.data(3) = '1' then
                        control_change <= '0';
                    end if;
                    if io_req_regs.data(4) = '1' then
                        dtr_change <= '0';
                    end if;
                when c_reg_slot_base =>
                    turbo_en <= io_req_regs.data(0);
                    slot_base <= unsigned(io_req_regs.data(6 downto 1));
                    nmi_selected <= io_req_regs.data(7);

                when others =>
                    null;
                end case;
            elsif io_req_regs.read='1' then
                io_resp_regs.ack <= '1';
                case io_req_regs.address(3 downto 0) is
                when c_reg_rx_head =>
                    io_resp_regs.data <= std_logic_vector(rx_head);
                when c_reg_rx_tail =>
                    io_resp_regs.data <= std_logic_vector(rx_tail);
                when c_reg_tx_head =>
                    io_resp_regs.data <= std_logic_vector(tx_head);
                when c_reg_tx_tail =>
                    io_resp_regs.data <= std_logic_vector(tx_tail);
                when c_reg_control =>
                    io_resp_regs.data <= control;
                when c_reg_command =>
                    io_resp_regs.data <= command;
                when c_reg_status =>
                    io_resp_regs.data <= status;
                when c_reg_enable =>
                    io_resp_regs.data(0) <= enable;
                    io_resp_regs.data(1) <= rx_irq_en;
                    io_resp_regs.data(2) <= tx_irq_en;
                    io_resp_regs.data(3) <= ctrl_irq_en;
                    io_resp_regs.data(4) <= hs_irq_en;
                when c_reg_handsh =>
                    io_resp_regs.data(0) <= cts;
                    io_resp_regs.data(1) <= rts;
                    io_resp_regs.data(2) <= not dsr_n;
                    io_resp_regs.data(3) <= dtr;
                    io_resp_regs.data(4) <= not dcd_n;
                    io_resp_regs.data(6) <= rx_pushback;
                when c_reg_irq_source =>
                    io_resp_regs.data(1) <= appl_rx_irq;
                    io_resp_regs.data(2) <= appl_tx_irq;
                    io_resp_regs.data(3) <= control_change;
                    io_resp_regs.data(4) <= dtr_change;
                when others =>
                    null;
                end case;
            end if;                     

            -- first cycle b_en = 1 and b_pending = 1
            -- then b_en = 0 and b_pending is still = 1. In this cycle RAM result is available.
            if b_pending = '1' then
                if b_en = '0' then
                    rx_full <= '1';
                    if rx_irq_disable = '0' then
                        irq <= '1';
                    end if;
                    rx_data <= b_rdata;
                    b_pending <= '0';
                end if;
            end if;

            dtr_d <= dtr;
            dsr_d <= dsr_n;
            dcd_d <= dcd_n;

            -- dtr change detect for application (drop on DTR low)
            if (dtr /= dtr_d) then
                dtr_change <= '1';
            end if;

            -- interrupts can only be generated when DTR = 1
            if dtr = '1' then
                if (dsr_d /= dsr_n) then
                    irq <= '1';
                end if;
                if (dcd_d /= dcd_n) then
                    irq <= '1';
                end if;             
            end if;

            -- when the ACIA is disabled (invisble in I/O range, IRQ is forced off)
            if enable = '0' then
                irq <= '0';
            end if;

            if reset = '1' then
                enable  <= '0';
                cts <= '0';
                dsr_n <= '1';
                dcd_n <= '1';
                tx_irq_en <= '0';
                rx_irq_en <= '0';
                ctrl_irq_en <= '0';
                hs_irq_en <= '0';
                dtr_change <= '0';
                control_change <= '0';
                slot_base <= (others => '0');
                nmi_selected <= '1';
                irq <= '0';
                turbo_en <= '0';
                turbo_sp <= "00";
                rx_pushback <= '1';
            end if;
            if c64_reset = '1' or reset = '1' then
                command <= X"02";
                control <= X"00";
                rx_head <= X"00";
                rx_tail <= X"00";
                tx_head <= X"00";
                tx_tail <= X"00";
                tx_empty <= '1';
                rx_full  <= '0';
                b_pending <= '0';
                irq <= '0';
            end if;
            if soft_reset = '1' then
                command(4 downto 0) <= "00010";
                overrun_err <= '0';
            end if;
        end if;
    end process;

    -- first we split our I/O bus in max 4 ranges, of 2K each.
    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 11,
        g_range_hi  => 12,
        g_ports     => 2 )
    port map (
        clock    => clock,
        
        req      => io_req,
        resp     => io_resp,
        
        reqs(0)  => io_req_regs,
        reqs(1)  => io_req_ram,
        
        resps(0) => io_resp_regs,
        resps(1) => io_resp_ram );


    process(clock)
    begin
        if rising_edge(clock) then
            io_ram_ack <= io_ram_en;
        end if;
    end process;
    io_ram_en        <= io_req_ram.read or io_req_ram.write;
    io_resp_ram.data <= X"00" when io_ram_ack='0' else io_ram_rdata;
    io_resp_ram.ack  <= io_ram_ack;
    
    i_ram: entity work.dpram
    generic map (
        g_width_bits            => 8,
        g_depth_bits            => 9,
        g_read_first_a          => false,
        g_read_first_b          => false,
        g_storage               => "block" )

    port map (
        a_clock                 => clock,
        a_address(8)            => io_req_ram.address(9), -- intentional mirroring
        a_address(7 downto 0)   => io_req_ram.address(7 downto 0),
        a_rdata                 => io_ram_rdata,
        a_wdata                 => io_req_ram.data,
        a_en                    => io_ram_en,
        a_we                    => io_req_ram.write,

        b_clock                 => clock,
        b_address               => b_address,
        b_rdata                 => b_rdata,
        b_wdata                 => b_wdata,
        b_en                    => b_en,
        b_we                    => b_we );

end architecture;

-- Conclusions from measurements:
-- ------------------------------
-- GENERAL
-- =======
-- - DCD change IRQ only happens when DTR is 1.
-- - IRQ remains set, even when DTR is switched off.
-- - IRQ can only be set when DTR is 1. This also applies to TxIRQ.

-- TRANSMIT
-- ========
-- - The transmitter is never turned off (at least not with the DTR), unlike what the datasheet claims.
-- - TxIRQ is not generated when DTR is off.
-- - Sending a character with the TxIRQ disabled, does not disable the IRQ flag, only reading the status does.

-- Thus:
-- * TxEmpty => current status in bit 4
-- * TxEmpty and TxIRQEnable and DTR='1' => Set IRQ bit (at all times, not only when TxEmpty _becomes_ 1)
-- * Reading Status clears the IRQ, but it would immediately get set again when TxEmpty = 1

-- RECEIVE
-- =======
-- Receive does not work when DTR is off. => bytes do not end up in data byte

-- When RxIRQ is off at the moment that a char arrives, IRQ is not set, not even after IRQ enable is set afterwards. However, when RxIRQ was on while the char
-- arrives, then the IRQ bit is set. The IRQ bit is cleared when the status byte is read
-- eventhough the RxFull remains set until the data byte is read.
-- Thus:
-- * DTR = 0 => no RxEvent (receiver disabled)
-- * RxEvent => Set RxFull (implies that DTR = 1)
-- * RxEvent and RxIRQEnable => set IRQ
-- * Read Status => clear IRQ
-- * Read Data => clear RxFull