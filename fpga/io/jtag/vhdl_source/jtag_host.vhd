--------------------------------------------------------------------------------
-- Entity: jtag_host
-- Date:2016-11-03  
-- Author: Gideon     
--
-- Description: AvalonMM based JTAG host, enhanced PIO.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity jtag_host is
generic (
    g_clock_divider : natural := 6 ); -- results in 6 low, 6 high cycles, or 5.2 MHz at 62.5 MHz
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- 32 bits Avalon bus interface
    -- We do up to 32 bits per transfer. The address determines the state of TMS and the length of the transfer
    -- All accesses are queued in a fifo. Read accesses result in a readdatavalid (eventually), and write
    -- accesses are simply queued. The host software can easily step through the JTAG states by
    -- issuing a couple of write commands. I.e. reset: issue a write command at address 9A (5*4) + 0x80 for TMS.
    -- 32*4 = 128. So the address mapping is as follows
    -- A7  A6..A2   A1..0
    -- TMS <length> <X>
    -- The data bits written are those that appear on TDI.

    avs_read            : in  std_logic;
    avs_write           : in  std_logic;
    avs_address         : in  std_logic_vector(7 downto 0);
    avs_writedata       : in  std_logic_vector(31 downto 0);
    avs_ready           : out std_logic;
    avs_readdata        : out std_logic_vector(31 downto 0);
    avs_readdatavalid   : out std_logic;

    jtag_tck            : out std_logic;
    jtag_tms            : out std_logic;
    jtag_tdi            : out std_logic;
    jtag_tdo            : in  std_logic );

end entity;

architecture arch of jtag_host is
    signal presc    : integer range 0 to g_clock_divider-1;
    type t_state is (idle, shifting_L, shifting_H);
    signal state    : t_state;
    signal sample   : std_logic;
    signal wr_en    : std_logic;
    signal rd_en    : std_logic;
    signal din      : std_logic_vector(38 downto 0);
    signal dout     : std_logic_vector(38 downto 0);
    signal full     : std_logic;
    signal valid    : std_logic;
    signal was_read : std_logic;
    signal tms_select : std_logic;
    signal data     : std_logic_vector(31 downto 0);
    signal length   : integer range 0 to 31;
    signal position : integer range 0 to 31;
begin
    i_fifo: entity work.sync_fifo
    generic map (
        g_depth        => 255,
        g_data_width   => 39,
        g_threshold    => 100,
        g_storage      => "auto",
        g_fall_through => true
    )
    port map(
        clock          => clock,
        reset          => reset,
        rd_en          => rd_en,
        wr_en          => wr_en,
        din            => din,
        dout           => dout,
        flush          => '0',
        full           => full,
        almost_full    => open,
        empty          => open,
        valid          => valid,
        count          => open
    );
    din   <= avs_read & avs_address(7 downto 2) & avs_writedata;
    wr_en <= avs_read or avs_write; 
    
    process(clock)
        variable v_bit  : std_logic;
    begin
        if rising_edge(clock) then
            avs_readdatavalid <= '0';
            rd_en <= '0';
            sample <= '0';
            if sample = '1' then
                data(position) <= jtag_tdo;
            end if;
            v_bit := data(position);
            
            case state is
            when idle =>
                presc <= g_clock_divider-1;
                jtag_tms <= '0';
                jtag_tck <= '0';
                jtag_tdi <= '0';

                if valid = '1' then
                    was_read   <= dout(38);
                    tms_select <= dout(37);
                    length     <= to_integer(unsigned(dout(36 downto 32)));
                    position   <= 0;
                    data       <= dout(31 downto 0);
                    state      <= shifting_L;
                    rd_en <= '1';
                end if;
            
            when shifting_L =>
                jtag_tck <= '0';
                if tms_select = '0' then
                    jtag_tdi <= v_bit;
                    jtag_tms <= '0';
                else
                    jtag_tdi <= '0';
                    jtag_tms <= v_bit;
                end if;                    
                if presc = 0 then
                    sample <= '1';
                    presc <= g_clock_divider-1;
                    state <= shifting_H;
                else
                    presc <= presc - 1;
                end if;
            
            when shifting_H =>
                jtag_tck <= '1';
                if presc = 0 then
                    presc <= g_clock_divider-1;
                    if position = length then
                        avs_readdatavalid <= was_read;
                        state <= idle;
                    else
                        position <= position + 1;
                        state <= shifting_L;
                    end if;                    
                else
                    presc <= presc - 1;
                end if;
            
            when others =>
                null;
            end case;            

            if reset = '1' then
                presc <= 0;
                length <= 0;
                position <= 0;
                state  <= idle;
            end if;
        end if;       
    end process;
    avs_readdata <= data;
    avs_ready    <= not full;
end architecture;
