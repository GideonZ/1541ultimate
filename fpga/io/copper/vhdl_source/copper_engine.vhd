-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- 
-- Author: Gideon Zweijtzer (gideon.zweijtzer (at) gmail.com)
--
-- Note that this file is copyrighted, and is not supposed to be used in other
-- projects without written permission from the author.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.slot_bus_pkg.all;
use work.dma_bus_pkg.all;
use work.copper_pkg.all;

entity copper_engine is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
                
    irq_n       : in  std_logic;
    phi2_tick   : in  std_logic;
    
    ram_address : out unsigned(10 downto 0);
    ram_rdata   : in  std_logic_vector(7 downto 0);
    ram_wdata   : out std_logic_vector(7 downto 0);
    ram_en      : out std_logic;
    ram_we      : out std_logic;

    trigger_1   : out std_logic;
    trigger_2   : out std_logic;

    dma_req     : out t_dma_req;
    dma_resp    : in  t_dma_resp;
                
    slot_req    : in  t_slot_req;
    slot_resp   : out t_slot_resp;
                
    control     : in  t_copper_control;
    status      : out t_copper_status );

end copper_engine;

architecture gideon of copper_engine is
    signal irq_c    : std_logic;
    signal irq_d    : std_logic;
    signal opcode   : std_logic_vector(7 downto 0);
    signal sync     : std_logic;
    signal timer    : unsigned(15 downto 0);
    signal match    : unsigned(15 downto 0);
    signal addr_i   : unsigned(10 downto 0);
    type t_state is (idle, fetch, decode, fetch2, decode2, fetch3, decode3, wait_irq, wait_sync, wait_until, wait_dma);
    signal state    : t_state;
begin
    p_fsm: process(clock)
    begin
        if rising_edge(clock) then
            irq_c <= not irq_n;
            irq_d <= irq_c;
            trigger_1 <= '0';
            trigger_2 <= '0';

            slot_resp <= c_slot_resp_init;
                        
            sync <= '0';
            if timer = control.frame_length then
                timer <= (others => '0');
                sync <= '1';
            elsif phi2_tick='1' then
                timer <= timer + 1;
            end if;

            case state is
            when idle =>
                addr_i <= (others => '0');

                case control.command is
                when c_copper_cmd_play =>
                    state <= fetch;
                    status.running <= '1';
                    
                when c_copper_cmd_record =>
                    null;
                    
                when others =>
                    null;
                end case;

            when fetch =>
                addr_i <= addr_i + 1;
                state <= decode;

            when decode =>
                opcode <= ram_rdata;
                if ram_rdata(7 downto 6) = c_copcode_write_reg(7 downto 6) then
                    state <= fetch2;
                elsif ram_rdata(7 downto 6) = c_copcode_read_reg(7 downto 6) then
                   dma_req.request <= '1';
                   dma_req.read_writen <= '1';
                   dma_req.address <= X"D0" & "00" & unsigned(ram_rdata(5 downto 0));
                   state <= wait_dma;
                else
                    case ram_rdata is
                    when c_copcode_wait_irq   => -- waits until falling edge of IRQn
                        state <= wait_irq;
                    when c_copcode_wait_sync  => -- waits until sync pulse from timer
                        state <= wait_sync;
                    when c_copcode_timer_clr  => -- clears timer 
                        timer <= (others => '0');
                        state <= fetch;
                    when c_copcode_capture    => -- copies timer to measure register
                        status.measured_time <= timer;
                        state <= fetch;
                    when c_copcode_wait_for   => -- takes a 1 byte argument
                        state <= fetch2;
                    when c_copcode_wait_until => -- takes 2 bytes argument (wait until timer match)
                        state <= fetch2;
                    when c_copcode_repeat     => -- restart at program address 0.    
                        addr_i <= (others => '0');
                        state <= fetch;
                    when c_copcode_end        => -- ends operation and return to idle
                        state <= idle;
                        status.running <= '0';
                    when c_copcode_trigger_1 =>
                        trigger_1 <= '1';
                        state <= fetch;
                    when c_copcode_trigger_2 =>
                        trigger_2 <= '1';
                        state <= fetch;
                    when others =>
                        state <= fetch;
                    end case;                    
                end if;

            when wait_irq =>
                if irq_c='1' and irq_d='0' then
                    state <= fetch;
                end if;

            when wait_sync =>
                if sync='1' then
                    state <= fetch;
                end if;

            when fetch2 =>
                addr_i <= addr_i + 1;
                state <= decode2;
                
            when decode2 =>
                if opcode(7 downto 6) = c_copcode_write_reg(7 downto 6) then
                    dma_req.request <= '1';
                    dma_req.read_writen <= '0';
                    dma_req.address <= X"D0" & "00" & unsigned(opcode(5 downto 0));
                    dma_req.data <= ram_rdata;
                    state <= wait_dma;
                else
                    case opcode is
                    when c_copcode_wait_for   => -- takes a 1 byte argument
                        match <= timer + unsigned(ram_rdata);
                        state <= wait_until;
                    when c_copcode_wait_until => -- takes 2 bytes argument (wait until timer match)
                        match(7 downto 0) <= unsigned(ram_rdata);
                        state <= fetch3;
                    when others =>
                        state <= fetch; -- illegal opcode
                    end case;                    
                end if;
                               
            when fetch3 =>
                addr_i <= addr_i + 1;
                state <= decode3;

            when decode3 =>
                -- the only opcode at this point requiring two bytes of opcode is the wait until..
                match(15 downto 8) <= unsigned(ram_rdata);
                state <= wait_until;
            
            when wait_until =>
                if timer = match then
                    state <= fetch;
                end if;

            when wait_dma =>
                if dma_resp.rack='1' then
                    dma_req.request <= '0';
                    state <= fetch;
                end if;

            when others =>
                null;
            end case;
                
            if control.stop='1' then
                state <= idle;
                status.running <= '0';
                dma_req.request <= '0';
            end if;

            if reset='1' then
                state   <= idle;
                timer   <= (others => '0');
                match   <= (others => '0');
                status  <= c_copper_status_init;
                dma_req <= c_dma_req_init;
            end if;
        end if;
    end process;
    
    ram_wdata   <= X"00";
    ram_address <= addr_i;
    ram_we      <= '0';
    ram_en      <= '1' when (state = fetch) or (state = fetch2) or (state = fetch3) else '0';
end architecture;
