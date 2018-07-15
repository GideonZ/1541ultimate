--------------------------------------------------------------------------------
-- Entity: usb_tracer
-- Date:2018-07-15  
-- Author: Gideon     
--
-- Description: Encodes USB data into 1480A compatible data format
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity usb_tracer is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    usb_data    : in  std_logic_vector(7 downto 0);
    usb_valid   : in  std_logic;
    usb_rxcmd   : in  std_logic;
    
    stream_out  : out std_logic_vector(31 downto 0);
    stream_valid: out std_logic );
end entity;

architecture arch of usb_tracer is
    signal counter  : unsigned(27 downto 0);

    signal raw_rd       : std_logic;    
    signal raw_wr       : std_logic;
    signal raw_valid    : std_logic;    
    signal raw_in       : std_logic_vector(37 downto 0);   
    signal raw_in       : std_logic_vector(37 downto 0);   

    signal enc16        : std_logic_vector(15 downto 0);
    signal enc16_push   : std_logic;
    type t_state is (idle, ext1, format1, format3a, format3b);
    signal state, next_state    : t_state;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            raw_wr  <= '0';
            if usb_valid = '1' then
                raw_in  <= '0' & usb_rxcmd & usb_data & std_logic_vector(counter);
                raw_wr  <= '1';
                counter <= to_unsigned(1, counter'length);
            elsif counter = X"FFFFFFF" then
                raw_in  <= "10" & X"00" & std_logic_vector(counter);
                raw_wr  <= '1';
                counter <= to_unsigned(1, counter'length);
            else
                counter <= counter + 1;
            end if;
            if reset = '1' then
                counter <= (others => '0');
            end if;    
        end if;
    end process;

    i_raw_fifo: entity work.sync_fifo
    generic map(
        g_depth        => 15,
        g_data_width   => 38,
        g_threshold    => 8,
        g_storage      => "auto",
        g_fall_through => true
    )
    port map(
        clock          => clock,
        reset          => reset,
        rd_en          => raw_rd,
        wr_en          => raw_wr,
        din            => raw_in,
        dout           => raw_out,
        flush          => '0',
        valid          => raw_valid
    );

    p_encode: process(raw_out, raw_valid, state)
    begin
        next_state <= state;
        enc16 <= (others => 'X');
        enc16_push <= '0';
        raw_rd <= '0';
        
        case state is
        when idle =>
            if raw_valid = '1' then
                if raw_out(37 downto 36) = "10" then
                    enc16 <= X"3FFF";
                    enc16_push <= '1';
                    raw_rd <= '1';
                    next_state <= ext1;
                elsif raw_out(27 downto 4) = X"000000" then -- Format 0 feasible
                    enc16 <= '1' & raw_out(36) & "00" & raw_out(3 downto 0) & raw_out(35 downto 28);
                    enc16_push <= '1';
                    raw_rd <= '1';
                elsif raw_out(27 downto 20) = X"000" then -- Format 1 or 2 are feasible                
                    enc16 <= '1' & raw_out(36) & "01" & raw_out(3 downto 0) & raw_out(11 downto 4);
                    enc16_push <= '1';
                    next_state <= format1;
                else
                    enc16 <= '1' & raw_out(36) & "11" & raw_out(3 downto 0) & raw_out(11 downto 4);
                    enc16_push <= '1';
                    next_state <= format3a;
                end if;
            end if;
        when ext1 =>
            enc16 <= X"FFFF";
            enc16_push <= '1';
            next_state <= idle;
        when format1 =>
            enc16 <= raw_out(35 downto 28) & raw_out(19 downto 12);
            enc16_push <= '1';
            raw_rd <= '1';
            next_state <= idle;
        when format3a =>
            enc16 <= raw_out(19 downto 12) & raw_out(27 downto 20);
            enc16_push <= '1';
            next_state <= format3b;
        when format3b =>
            enc16 <= X"00" & raw_out(35 downto 28);
            enc16_push <= '1';
            raw_rd <= '1';
            next_state <= idle;
        when others =>
            null;
        end case;
    end process;
    
    process(clock)
    begin
        if rising_edge(clock) then
            state <= next_state;
            stream_valid <= '0';
            
            if enc16_push = '1' then
                if toggle = '0' then
                    stream_out(15 downto 0) <= enc16;
                    toggle <= '1';
                else
                    stream_out(31 downto 16) <= enc16;
                    stream_valid <= '1';
                    toggle <= '0';
                end if;
            end if;
            if reset = '1' then
                state <= idle;
                toggle <= '0';
                stream_out <= (others => '0');
            end if;
        end if;
    end process;
end architecture;
