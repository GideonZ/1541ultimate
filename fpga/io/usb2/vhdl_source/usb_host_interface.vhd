
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.usb_pkg.all;

entity usb_host_interface is
generic (
    g_simulation    : boolean := false );
port (
    clock       : in    std_logic;
    reset       : in    std_logic;

    usb_rx      : out   t_usb_rx;
    usb_tx_req  : in    t_usb_tx_req;
    usb_tx_resp : out   t_usb_tx_resp;

    -- low level ulpi interfacing
    reg_read    : in  std_logic := '0';
    reg_write   : in  std_logic;
    reg_address : in  std_logic_vector(5 downto 0);
    reg_wdata   : in  std_logic_vector(7 downto 0);
    reg_rdata   : out std_logic_vector(7 downto 0);
    reg_ack     : out std_logic;

    do_chirp    : in  std_logic := '0';
    chirp_data  : in  std_logic := '0';
    status      : out std_logic_vector(7 downto 0);
    speed       : in  std_logic_vector(1 downto 0);

    ulpi_nxt    : in    std_logic;
    ulpi_stp    : out   std_logic;
    ulpi_dir    : in    std_logic;
    ulpi_data   : inout std_logic_vector(7 downto 0) );
end entity;

architecture structural of usb_host_interface is
    signal status_i     : std_logic_vector(7 downto 0);
    
    signal tx_data      : std_logic_vector(7 downto 0) := X"00";
    signal tx_last      : std_logic := '0';
    signal tx_valid     : std_logic := '0';
    signal tx_start     : std_logic := '0';
    signal tx_next      : std_logic := '0';
    
    signal rx_data      : std_logic_vector(7 downto 0) := X"00";
    signal rx_register  : std_logic := '0';
    signal rx_last      : std_logic := '0';
    signal rx_valid     : std_logic := '0';
    signal rx_store     : std_logic := '0';

    signal rx_crc_sync   : std_logic;
    signal rx_crc_dvalid : std_logic;
    signal tx_crc_sync   : std_logic;
    signal tx_crc_dvalid : std_logic;
    signal crc_sync      : std_logic;
    signal crc_dvalid    : std_logic;
    signal tx_data_to_crc: std_logic_vector(7 downto 0);
    signal crc_data_in   : std_logic_vector(7 downto 0);
    signal data_crc      : std_logic_vector(15 downto 0);
begin
    i_ulpi: entity work.ulpi_bus
        port map (
            clock       => clock,
            reset       => reset,
            
            ULPI_DATA   => ulpi_data,
            ULPI_DIR    => ulpi_dir,
            ULPI_NXT    => ulpi_nxt,
            ULPI_STP    => ulpi_stp,
            
            -- status
            status      => status_i,
            operational => '1',
        
            -- chirp interface
            do_chirp    => do_chirp,
            chirp_data  => chirp_data,
            
            -- register interface
            reg_read    => reg_read,
            reg_write   => reg_write,
            reg_address => reg_address,
            reg_wdata   => reg_wdata,
            reg_ack     => reg_ack,
            
            -- stream interface
            tx_data     => tx_data,
            tx_last     => tx_last,
            tx_valid    => tx_valid,
            tx_start    => tx_start,
            tx_next     => tx_next,
        
            rx_data     => rx_data,
            rx_register => rx_register,
            rx_last     => rx_last,
            rx_valid    => rx_valid,
            rx_store    => rx_store );

    i_rx: entity work.ulpi_rx
        generic map (
            g_support_split  => false,
            g_support_token  => false ) -- hosts do not receive tokens
        port map (
            clock           => clock,
            reset           => reset,
                            
            rx_data         => rx_data,
            rx_last         => rx_last,
            rx_valid        => rx_valid,
            rx_store        => rx_store,
                            
            status          => status_i,

            -- interface to DATA CRC (shared resource)
            crc_sync        => rx_crc_sync,
            crc_dvalid      => rx_crc_dvalid,
            data_crc        => data_crc,
            
            usb_rx          => usb_rx );

    crc_sync    <= rx_crc_sync or tx_crc_sync;
    crc_dvalid  <= rx_crc_dvalid or tx_crc_dvalid;
    crc_data_in <= rx_data when rx_crc_dvalid='1' else tx_data_to_crc; 

    i_data_crc: entity work.data_crc
    port map (
        clock       => clock,
        sync        => crc_sync,
        valid       => crc_dvalid,
        data_in     => crc_data_in,
        crc         => data_crc );

    i_tx: entity work.ulpi_tx 
        generic map (
            g_simulation     => g_simulation,
            g_support_split  => true,
            g_support_token  => true ) -- hosts do send tokens
        port map (
            clock       => clock,
            reset       => reset,
            
            -- Bus Interface
            tx_start    => tx_start,
            tx_last     => tx_last,
            tx_valid    => tx_valid,
            tx_next     => tx_next,
            tx_data     => tx_data,
            rx_busy     => rx_store,
            
            -- interface to DATA CRC (shared resource)
            crc_sync    => tx_crc_sync,
            crc_dvalid  => tx_crc_dvalid,
            data_crc    => data_crc,
            data_to_crc => tx_data_to_crc,
            
            -- Status
            status      => status_i,
            speed       => speed,
            
            -- Interface to send tokens and handshakes
            usb_tx_req  => usb_tx_req,
            usb_tx_resp => usb_tx_resp );

    status <= status_i;
    reg_rdata <= rx_data;

end architecture;
