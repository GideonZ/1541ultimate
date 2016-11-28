-------------------------------------------------------------------------------
-- Title      : avalon_to_io_bridge
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module bridges the avalon bus to I/O
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;


entity avalon_to_io_bridge is
port (
    clock               : in  std_logic;
    reset               : in  std_logic;
    
    -- 8 bits Avalon bus interface
    avs_read            : in  std_logic;
    avs_write           : in  std_logic;
    avs_address         : in  std_logic_vector(19 downto 0);
    avs_writedata       : in  std_logic_vector(7 downto 0);
    avs_ready           : out std_logic;
    avs_readdata        : out std_logic_vector(7 downto 0);
    avs_readdatavalid   : out std_logic;
    avs_irq             : out std_logic;

    io_read             : out std_logic;
    io_write            : out std_logic;
    io_address          : out std_logic_vector(19 downto 0);
    io_wdata            : out std_logic_vector(7 downto 0);
    io_rdata            : in  std_logic_vector(7 downto 0);
    io_ack              : in  std_logic;
    io_irq              : in  std_logic );

end entity;

architecture rtl of avalon_to_io_bridge is
    type t_state is (idle, read_pending, write_pending);
    signal state    : t_state;
begin
    avs_irq           <= io_irq;
    avs_ready         <= '1' when (state = idle) else '0';
    avs_readdatavalid <= '1' when (state = read_pending) and (io_ack = '1') else '0';
    avs_readdata      <= io_rdata;

    process(clock)
    begin
        if rising_edge(clock) then
            io_read  <= '0';
            io_write <= '0';
            case state is
            when idle =>
                io_wdata     <= avs_writedata;
                io_address   <= avs_address;
                if avs_read='1' then
                    io_read <= '1';
                    state <= read_pending;
                elsif avs_write='1' then
                    io_write <= '1';
                    state <= write_pending;
                end if;
            when read_pending =>
                if io_ack = '1' then
                    state <= idle;
                end if;
            when write_pending =>
                if io_ack = '1' then
                    state <= idle;
                end if;
            when others =>
                null;
            end case;
                
            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;

end architecture;
