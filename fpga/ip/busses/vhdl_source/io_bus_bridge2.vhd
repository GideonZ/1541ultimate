library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;

entity io_bus_bridge2 is
generic (
    g_addr_width : natural := 8 );
port (
    clock_a   : in  std_logic;
    reset_a   : in  std_logic;
    req_a     : in  t_io_req;
    resp_a    : out t_io_resp;
    
    clock_b   : in  std_logic;
    reset_b   : in  std_logic;
    req_b     : out t_io_req;
    resp_b    : in  t_io_resp );
end entity;

architecture rtl of io_bus_bridge2 is
    constant c_io_bus_req_vector_width  : natural := g_addr_width + 10;
      
    signal request_a        : std_logic;
    signal req_vector_a     : std_logic_vector(c_io_bus_req_vector_width-1 downto 0);
    signal request_b        : std_logic;
    signal req_vector_b     : std_logic_vector(c_io_bus_req_vector_width-1 downto 0);
    signal address_b        : unsigned(19 downto 0) := (others => '0');    
    signal resp_vector_a    : std_logic_vector(7 downto 0);
    signal response_a       : std_logic;
begin
    req_vector_a <= std_logic_vector(req_a.address(g_addr_width-1 downto 0)) & req_a.data & req_a.read & req_a.write;
    request_a    <= req_a.write or req_a.read;

    i_heen: entity work.synchronizer_gzw
    generic map(
        g_width     => req_vector_a'length,
        g_fast      => true
    )
    port map(
        tx_clock    => clock_a,
        tx_push     => request_a,
        tx_data     => req_vector_a,
        tx_done     => open,
        rx_clock    => clock_b,
        rx_new_data => request_b,
        rx_data     => req_vector_b );

    address_b(g_addr_width-1 downto 0) <= unsigned(req_vector_b(9 + g_addr_width downto 10)); 
    req_b.address <= address_b;
    req_b.data    <= req_vector_b(9 downto 2);  
    req_b.read    <= req_vector_b(1) and request_b;
    req_b.write   <= req_vector_b(0) and request_b;

    i_terug: entity work.synchronizer_gzw
    generic map(
        g_width     => 8,
        g_fast      => true
    )
    port map(
        tx_clock    => clock_b,
        tx_push     => resp_b.ack,
        tx_data     => resp_b.data,
        tx_done     => open,
        rx_clock    => clock_a,
        rx_new_data => response_a,
        rx_data     => resp_vector_a );

    resp_a.ack <= response_a;
    resp_a.data <= resp_vector_a when response_a = '1' else X"00";

end rtl;
