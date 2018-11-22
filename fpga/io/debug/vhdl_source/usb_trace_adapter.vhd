--------------------------------------------------------------------------------
-- Entity: usb_trace_adapter
-- Date:2018-07-15  
-- Author: Gideon     
--
-- Description: Encodes USB data into 1480A compatible data format
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity usb_trace_adapter is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    rx_data     : in  std_logic_vector(7 downto 0);
    rx_cmd      : in  std_logic;
    rx_ourdata  : in  std_logic;
    rx_store    : in  std_logic;
    tx_first    : in  std_logic;
    
    usb_data    : out std_logic_vector(7 downto 0);
    usb_valid   : out std_logic;
    usb_rxcmd   : out std_logic );

end entity;

architecture arch of usb_trace_adapter is
    signal tx_latch : std_logic;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if tx_first = '1' and tx_latch = '0' then
                tx_latch <= '1';
            elsif rx_ourdata = '1' then
                tx_latch <= '0';
            end if; 

            -- What is what?
            -- Case 1: We are sending: first byte is PID, we need to translate it
            usb_rxcmd <= '0';
            usb_data <= rx_data;

            if rx_ourdata = '1' then
                usb_valid <= '1';
                if tx_latch = '1' then
                    usb_data <= not(rx_data(3 downto 0)) & rx_data(3 downto 0);
                end if;
            elsif rx_cmd = '1' then
                usb_rxcmd <= '1';
                usb_valid <= '1';
            elsif rx_store = '1' then
                usb_valid <= '1';
            end if;
            
            if reset = '1' then
                tx_latch <= '0';
            end if;
        end if;
    end process;

end architecture;
