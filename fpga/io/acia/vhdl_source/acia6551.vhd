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
    
    -- C64 side interface
    slot_tick       : in  std_logic;
    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp;
        
    -- io interface for local cpu
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq          : out std_logic );

end entity;

architecture arch of acia6551 is
    signal slot_base        : unsigned(8 downto 2) := (others => '0');
    signal rx_data          : std_logic_vector(7 downto 0);
    signal status           : std_logic_vector(7 downto 0) := X"00";
    signal command          : std_logic_vector(7 downto 0);
    signal control          : std_logic_vector(7 downto 0);

    signal tx_data          : std_logic_vector(7 downto 0);
    signal tx_data_push     : std_logic;
    signal rx_data_valid    : std_logic;
    
    alias irq           : std_logic is status(7);
    alias dsr_n         : std_logic is status(6);
    alias dcd_n         : std_logic is status(5);
    alias tx_empty      : std_logic is status(4);
    alias rx_full       : std_logic is status(3);
    alias overrun_err   : std_logic is status(2);
    alias framing_err   : std_logic is status(1);
    alias parity_err    : std_logic is status(0);

    alias dtr           : std_logic is command(0);
    signal dtr_d, rts_d     : std_logic;
    
    signal enable           : std_logic;
    signal nmi_selected     : std_logic;
    signal tx_irq_en        : std_logic;
    signal rx_irq_en        : std_logic;
    signal soft_reset       : std_logic;
    signal rx_interrupt     : std_logic := '0';
    signal tx_interrupt     : std_logic := '0';
    signal appl_tx_irq      : std_logic := '0';
    signal appl_rx_irq      : std_logic := '0';
    signal ctrl_irq_en      : std_logic;
    signal hs_irq_en        : std_logic;
    signal control_change   : std_logic;
    signal rts_dtr_change   : std_logic;
    
    signal cts              : std_logic; -- written by sys
    signal rts              : std_logic; -- written by slot (command register)

    signal rx_head, rx_tail : unsigned(7 downto 0);
    signal tx_head, tx_tail : unsigned(7 downto 0);

    signal rx_rate          : unsigned(7 downto 0);
    signal rx_rate_cnt      : unsigned(12 downto 0) := (others => '0');
    signal rx_rate_expired  : std_logic := '1';

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
begin
    with slot_req.bus_address(1 downto 0) select slot_resp.data <=
        rx_data     when c_addr_data_register,
        status      when c_addr_status_register,
        command     when c_addr_command_register, 
        control     when c_addr_control_register,
        X"FF"       when others;   

    slot_resp.reg_output <= enable when slot_req.bus_address(8 downto 2) = slot_base else '0';
    slot_resp.irq  <= irq and not nmi_selected;
    slot_resp.nmi  <= irq and nmi_selected;

    irq       <= enable and (rx_interrupt and not command(1));-- or (tx_interrupt and command(2) and not command(3)));
    rts       <= command(2) or command(3);

    rx_full   <= rx_data_valid when rising_edge(clock); -- to have a register for the status word in signaltap
    --tx_empty  <= '0' when (tx_head + 1) = tx_tail else '1';

    -- IRQs to the Host (Slot side)
    tx_interrupt <= tx_empty;
    -- rx_interrupt <= (rx_data_valid and rts) when rx_rate_cnt = X"00" else '0';
    
    -- IRQs to the Application (IO side)
    appl_rx_irq  <= '0' when (rx_head + 1) = rx_tail else '1'; -- RX = Appl -> Host (room for data appl can write)
    appl_tx_irq  <= '1' when tx_head /= tx_tail else '0';      -- TX = Host -> Appl (data appl should read)
    io_irq       <= (appl_rx_irq and rx_irq_en) or (appl_tx_irq and tx_irq_en) or
                    (control_change and ctrl_irq_en) or (rts_dtr_change and hs_irq_en);
    
    process(clock)
    begin
        if rising_edge(clock) then
            soft_reset <= '0';
            tx_data_push <= '0';
            rts_d <= rts;
            dtr_d <= dtr;
            if tx_head + 1 = tx_tail then
                tx_empty <= '0';
            else
                tx_empty <= '1';
            end if;
                 
            b_en <= '0';
            b_we <= '0';
            b_address <= (others => 'X');
            b_wdata <= (others => 'X');

            if slot_tick = '1' and rx_rate_expired = '0' then
                if rx_rate_cnt = 0 then
                    rx_rate_expired <= '1';
                else
                    rx_rate_cnt <= rx_rate_cnt - 1;
                end if;
            end if;

            if tx_data_push = '1' and tx_empty = '1' and dtr = '1' then
                b_address <= '0' & tx_head;
                b_wdata <= tx_data;
                b_we <= '1';
                b_en <= '1';
                tx_head <= tx_head + 1;
            elsif rx_data_valid = '0' and rx_head /= rx_tail and b_pending = '0' and dtr = '1' and rts = '1' and rx_rate_expired = '1' then
                rx_rate_expired <= '0';
                rx_rate_cnt <= rx_rate & "00011";
                b_address <= '1' & rx_tail;
                b_en <= '1';
                b_pending <= '1';
                rx_tail <= rx_tail + 1;
            end if;

            if (slot_req.io_address(8 downto 2) = slot_base) and (enable = '1') then
                if slot_req.io_write='1' then
                    case slot_req.io_address(1 downto 0) is
                    when c_addr_data_register =>
                        tx_data <= slot_req.data;
                        tx_data_push <= '1';
                    when c_addr_status_register =>
                        soft_reset <= '1';
                    when c_addr_command_register =>
                        command <= slot_req.data;
                    when c_addr_control_register =>
                        control <= slot_req.data;
                        control_change <= '1';
                    when others =>
                        null;
                    end case;
                elsif slot_req.io_read='1' then
                    case slot_req.io_address(1 downto 0) is
                    when c_addr_data_register =>
                        parity_err <= '0';
                        framing_err <= '0';
                        overrun_err <= '0';
                        rx_data_valid <= '0';
                    when c_addr_status_register =>
                        rx_interrupt <= '0';
                        null;
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
                when c_reg_irq_source =>
                    if io_req_regs.data(3) = '1' then
                        control_change <= '0';
                    end if;
                    if io_req_regs.data(4) = '1' then
                        rts_dtr_change <= '0';
                    end if;
                when c_reg_slot_base =>
                    slot_base <= unsigned(io_req_regs.data(6 downto 0));
                    nmi_selected <= io_req_regs.data(7);
                when c_reg_rx_rate =>
                    rx_rate <= unsigned(io_req_regs.data);
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
                when c_reg_irq_source =>
                    io_resp_regs.data(1) <= appl_rx_irq;
                    io_resp_regs.data(2) <= appl_tx_irq;
                    io_resp_regs.data(3) <= control_change;
                    io_resp_regs.data(4) <= rts_dtr_change;
                when c_reg_slot_base =>
                    io_resp_regs.data(6 downto 0) <= std_logic_vector(slot_base);
                    io_resp_regs.data(7) <= nmi_selected;
                when c_reg_rx_rate =>
                    io_resp_regs.data <= std_logic_vector(rx_rate);
                when others =>
                    null;
                end case;
            end if;                     

            -- first cycle b_en = 1 and b_pending = 1
            -- then b_en = 0 and b_pending is still = 1. In this cycle RAM result is available.
            if b_pending = '1' then
                if b_en = '0' then
                    rx_data_valid <= '1';
                    rx_interrupt <= '1';
                    rx_data <= b_rdata;
                    b_pending <= '0';
                end if;
            end if;

            if (dtr /= dtr_d) or (rts /= rts_d) then
                rts_dtr_change <= '1';
            end if;

            if reset = '1' then
                command <= X"02";
                control <= X"00";
                rx_head <= X"00";
                rx_tail <= X"00";
                tx_head <= X"00";
                tx_tail <= X"00";
                enable  <= '0';
                b_pending <= '0';
                rx_data_valid <= '0';
                rx_interrupt <= '0';
                cts <= '0';
                dsr_n <= '1';
                dcd_n <= '1';
                tx_irq_en <= '0';
                rx_irq_en <= '0';
                ctrl_irq_en <= '0';
                hs_irq_en <= '0';
                rts_dtr_change <= '0';
                control_change <= '0';
                slot_base <= (others => '0');
                rx_rate <= X"82";
            end if;
            if soft_reset = '1' or c64_reset = '1' then
                command(4 downto 0) <= "00010";
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
