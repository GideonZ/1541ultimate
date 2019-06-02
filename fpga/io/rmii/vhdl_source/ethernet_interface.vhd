-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : ethernet_interface
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Filter block for ethernet frames
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.io_bus_pkg.all;
    use work.block_bus_pkg.all;
    use work.mem_bus_pkg.all;

entity ethernet_interface is
generic (
    g_mem_tag       : std_logic_vector(7 downto 0) := X"12" );
port (
    sys_clock       : in  std_logic;
    sys_reset       : in  std_logic;
        
    -- io interface for local cpu
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq_tx       : out std_logic;
    io_irq_rx       : out std_logic;
    
    -- interface to memory
    mem_req         : out t_mem_req_32;
    mem_resp        : in  t_mem_resp_32;

    ----
    eth_clock       : in std_logic;
    eth_reset       : in std_logic;

    eth_tx_data     : out std_logic_vector(7 downto 0);
    eth_tx_sof      : out std_logic := '0';
    eth_tx_eof      : out std_logic;
    eth_tx_valid    : out std_logic;
    eth_tx_ready    : in  std_logic;

    eth_rx_data     : in  std_logic_vector(7 downto 0);
    eth_rx_sof      : in  std_logic;
    eth_rx_eof      : in  std_logic;
    eth_rx_valid    : in  std_logic );

end entity;

architecture gideon of ethernet_interface is
    signal io_req_free     : t_io_req;
    signal io_resp_free    : t_io_resp;
    signal io_req_rx       : t_io_req;
    signal io_resp_rx      : t_io_resp;
    signal io_req_tx       : t_io_req;
    signal io_resp_tx      : t_io_resp;

    signal alloc_req       : std_logic := '0';
    signal alloc_resp      : t_alloc_resp;
    signal used_req        : t_used_req;
    signal used_resp       : std_logic;
    signal mem_req_tx      : t_mem_req_32;
    signal mem_resp_tx     : t_mem_resp_32;
    signal mem_req_rx      : t_mem_req_32;
    signal mem_resp_rx     : t_mem_resp_32;

begin
    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 4,
        g_range_hi => 5,
        g_ports    => 3
    )
    port map (
        clock      => sys_clock,
        req        => io_req,
        resp       => io_resp,
        reqs(2)    => io_req_free,
        reqs(1)    => io_req_tx,
        reqs(0)    => io_req_rx,
        resps(2)   => io_resp_free,
        resps(1)   => io_resp_tx,
        resps(0)   => io_resp_rx  );
    
    i_rx: entity work.eth_filter
    generic map (
        g_mem_tag       => g_mem_tag
    )
    port map (
        clock           => sys_clock,
        reset           => sys_reset,
        io_req          => io_req_rx,
        io_resp         => io_resp_rx,
        alloc_req       => alloc_req,
        alloc_resp      => alloc_resp,
        used_req        => used_req,
        used_resp       => used_resp,
        mem_req         => mem_req_rx,
        mem_resp        => mem_resp_rx,

        eth_clock       => eth_clock,
        eth_reset       => eth_reset,
        eth_rx_data     => eth_rx_data,
        eth_rx_sof      => eth_rx_sof,
        eth_rx_eof      => eth_rx_eof,
        eth_rx_valid    => eth_rx_valid );
    
    i_tx: entity work.eth_transmit
    generic map (
        g_mem_tag       => g_mem_tag xor X"01"
    )
    port map (
        clock           => sys_clock,
        reset           => sys_reset,
        io_req          => io_req_tx,
        io_resp         => io_resp_tx,
        io_irq          => io_irq_tx,
        mem_req         => mem_req_tx,
        mem_resp        => mem_resp_tx,

        eth_clock       => eth_clock,
        eth_reset       => eth_reset,
        eth_tx_data     => eth_tx_data,
        eth_tx_sof      => eth_tx_sof,
        eth_tx_eof      => eth_tx_eof,
        eth_tx_valid    => eth_tx_valid,
        eth_tx_ready    => eth_tx_ready );
    
    i_free: entity work.free_queue
    generic map(
        g_store_size    => true,
        g_block_size    => 1536
    )
    port map (
        clock           => sys_clock,
        reset           => sys_reset,
        alloc_req       => alloc_req,
        alloc_resp      => alloc_resp,
        used_req        => used_req,
        used_resp       => used_resp,
        io_req          => io_req_free,
        io_resp         => io_resp_free,
        io_irq          => io_irq_rx  );

    i_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_registered    => true,
        g_ports         => 2
    )
    port map(
        clock           => sys_clock,
        reset           => sys_reset,
        reqs(0)         => mem_req_rx,
        reqs(1)         => mem_req_tx,
        resps(0)        => mem_resp_rx,
        resps(1)        => mem_resp_tx,
        req             => mem_req,
        resp            => mem_resp );
    

end architecture;
