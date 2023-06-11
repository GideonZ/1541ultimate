-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2022 - Gideon's Logic B.V.
--
-------------------------------------------------------------------------------
-- Title      : CS8900A bus interface module
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements the bus-behavior of the CS8900A chip.
--              It is based on a dual ported memory, which can be read/written
--              from the cartridge port, as well as from the other CPU as I/O
--              device. This allows the software to emulate the functionality
--              of the link, while this hardware block only implements how the
--              chip behaves as seen from the cartridge port.
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.slot_bus_pkg.all;

entity cs8900a_bus is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
                    
    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp;
    
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp );
    
end entity;

architecture gideon of cs8900a_bus is
    signal enable          : std_logic;
    signal bus_waddr       : std_logic_vector(3 downto 0);
    signal bus_raddr       : std_logic_vector(3 downto 0);
    signal bus_write       : std_logic;
    signal bus_read        : std_logic;
    signal bus_wdata       : std_logic_vector(7 downto 0);
    signal bus_rdata       : std_logic_vector(7 downto 0);
                    
    signal pp_addr         : unsigned(11 downto 0);
    signal pp_write        : std_logic;
    signal pp_read         : std_logic;
    signal pp_tx_data      : std_logic; -- put
    signal pp_rx_data      : std_logic; -- get
    signal pp_wdata        : std_logic_vector(15 downto 0);
    signal pp_rdata        : std_logic_vector(15 downto 0);
    signal pp_new_rx_pkt   : std_logic;

    -- The 8900A chip is accessed in WORDs, using alternately
    -- even and odd bytes. Only PacketPage access in I/O mode
    -- is supported.
    constant c_rx_tx_data_0         : std_logic_vector(3 downto 1) := "000"; -- R/W
    constant c_rx_tx_data_1         : std_logic_vector(3 downto 1) := "001"; -- R/W
    constant c_tx_command           : std_logic_vector(3 downto 1) := "010"; -- W
    constant c_tx_length            : std_logic_vector(3 downto 1) := "011"; -- W
    constant c_isq                  : std_logic_vector(3 downto 1) := "100"; -- R
    constant c_packet_page_pointer  : std_logic_vector(3 downto 1) := "101"; -- R/W
    constant c_packet_page_data_0   : std_logic_vector(3 downto 1) := "110"; -- R/W
    constant c_packet_page_data_1   : std_logic_vector(3 downto 1) := "111"; -- R/W

    constant c_lo_rx_tx_data_0          : std_logic_vector(3 downto 0) := "0000"; -- R/W
    constant c_hi_rx_tx_data_0          : std_logic_vector(3 downto 0) := "0001"; -- R/W
    constant c_lo_rx_tx_data_1          : std_logic_vector(3 downto 0) := "0010"; -- R/W
    constant c_hi_rx_tx_data_1          : std_logic_vector(3 downto 0) := "0011"; -- R/W
    constant c_lo_packet_page_pointer   : std_logic_vector(3 downto 0) := "1010"; -- R/W
    constant c_hi_packet_page_pointer   : std_logic_vector(3 downto 0) := "1011"; -- R/W
    constant c_lo_packet_page_data_0    : std_logic_vector(3 downto 0) := "1100"; -- R/W
    constant c_hi_packet_page_data_0    : std_logic_vector(3 downto 0) := "1101"; -- R/W
    constant c_lo_packet_page_data_1    : std_logic_vector(3 downto 0) := "1110"; -- R/W
    constant c_hi_packet_page_data_1    : std_logic_vector(3 downto 0) := "1111"; -- R/W

 
    signal packet_page_pointer      : unsigned(11 downto 1);
    signal packet_page_auto_inc     : std_logic;
    signal word_buffer              : std_logic_vector(15 downto 0);
    signal rx_count                 : integer range 0 to 2;
begin
    b_bus: block
        signal sel : std_logic;
    begin
        sel        <= enable when 
                        slot_req.io_address(8 downto 4) = "00000" and
                        slot_req.io_address(3 downto 1) /= "000"
                 else '0';
        bus_waddr  <= slot_req.io_address(3 downto 0);
        bus_raddr  <= slot_req.bus_address(3 downto 0);
        bus_write  <= slot_req.io_write and sel;
        bus_read   <= slot_req.io_read and sel;
        bus_wdata  <= slot_req.data;

        slot_resp  <= c_slot_resp_init;
        slot_resp.data <= bus_rdata;
        slot_resp.reg_output <= sel;
    end block;

    pp_wdata <= word_buffer;
    
    process(clock)
        variable v_3bit_addr    : std_logic_vector(3 downto 1);
    begin
        if rising_edge(clock) then
            -- handle writes
            pp_write   <= '0';
            pp_read    <= '0';
            pp_rx_data <= '0';
            pp_tx_data <= '0';
            pp_addr    <= packet_page_pointer & '0';
            
            v_3bit_addr := bus_addr(3 downto 1);
            
            -- determine pp_addr for reads (default, will be overwritten by writes)
            if bus_addr(3 downto 2)="00" then
                case rx_count is
                when 0 =>
                    pp_addr <= X"400";
                when 1 =>
                    pp_addr <= X"402";
                when others =>
                    pp_addr <= X"404";
                end case;
                
                if bus_read='1' and bus_addr(0)='1' then -- read from odd address
                    if rx_count /= 2 then
                        rx_count <= rx_count + 1;
                        pp_read <= '1';
                    else
                        pp_rx_data <= '1'; -- pop
                    end if;
                end if;
            end if;
                                
            if bus_write='1' then
                if bus_addr(0)='0' then
                    word_buffer(7 downto 0) <= bus_wdata;
                else
                    word_buffer(15 downto 8) <= bus_wdata;
                    
                    case v_3bit_addr is
                        when c_rx_tx_data_0 | c_rx_tx_data_1 =>
                            pp_tx_data <= '1';
                            pp_write <= '1';
                            pp_addr <= X"A00";
                        when c_tx_command =>
                            pp_addr <= X"144";
                            pp_write <= '1';
                        when c_tx_length =>
                            pp_addr <= X"146";
                            pp_write <= '1';
                        when c_packet_page_pointer =>
                            packet_page_pointer <= unsigned(word_buffer(packet_page_pointer'range));
                            packet_page_auto_inc <= word_buffer(15);
                        when c_packet_page_data_0 | c_packet_page_data_1 =>
                            pp_write <= '1';
                            if packet_page_auto_inc='1' then
                                packet_page_pointer <= packet_page_pointer + 1;
                            end if;
                        when others =>
                            null;
                    end case;
                end if;
            end if;
            
            if pp_new_tx_pkt='1' then
                rx_count <= 0;
            end if;

            if reset='1' then
                packet_page_pointer <= (others => '0');
                packet_page_auto_inc <= '0';
            end if;
                    
        end if;
    end process;

    -- determine output byte  (combinatorial, since it's easy!)
    with bus_addr select bus_rdata <=
        pp_rdata(7 downto 0)  when c_lo_rx_tx_data_0 | c_lo_rx_tx_data_1 | c_lo_packet_page_data_0 | c_lo_packet_page_data_1,
        pp_rdata(15 downto 8) when c_hi_rx_tx_data_0 | c_hi_rx_tx_data_1 | c_hi_packet_page_data_0 | c_hi_packet_page_data_1,
        std_logic_vector(packet_page_pointer(7 downto 1)) & '0' when c_lo_packet_page_pointer,
        packet_page_auto_inc & "011" & std_logic_vector(packet_page_pointer(11 downto 8)) when c_hi_packet_page_pointer,
        X"00" when others;

end;
