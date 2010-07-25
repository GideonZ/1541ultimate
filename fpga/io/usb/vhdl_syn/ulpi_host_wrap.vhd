library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity ulpi_host_wrap is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    -- Transmit Path Interface
    tx_busy     : in  std_logic;
    tx_ack      : in  std_logic;
    
    -- Interface to send tokens and handshakes
    send_token  : out std_logic;
    send_handsh : out std_logic;
    tx_pid      : out std_logic_vector(3 downto 0);
    tx_token    : out std_logic_vector(10 downto 0);
    
    -- Interface to send data packets
    send_data   : out std_logic;
    no_data     : out std_logic;
    user_data   : out std_logic_vector(7 downto 0);
    user_last   : out std_logic;
    user_valid  : out std_logic;
    user_next   : in  std_logic;
            
    -- Interface to bus initialization unit
    do_reset    : out std_logic;
    reset_done  : in  std_logic;
    speed       : in  std_logic_vector(1 downto 0);

    reset_pkt   : in  std_logic;
    reset_data  : in  std_logic_vector(7 downto 0);
    reset_last  : in  std_logic;
    reset_valid : in  std_logic;
    
    -- Receive Path Interface
    rx_pid          : in  std_logic_vector(3 downto 0);
    rx_token        : in  std_logic_vector(10 downto 0);
    valid_token     : in  std_logic;
    valid_handsh    : in  std_logic;

    valid_packet    : in  std_logic;
    data_valid      : in  std_logic;
    data_start      : in  std_logic;
    data_out        : in  std_logic_vector(7 downto 0);

    rx_error        : in  std_logic );
        
end ulpi_host_wrap;

architecture wrap of ulpi_host_wrap is
    signal descr_addr      : std_logic_vector(8 downto 0);
    signal descr_rdata     : std_logic_vector(31 downto 0);
    signal descr_wdata     : std_logic_vector(31 downto 0);
    signal descr_en        : std_logic;
    signal descr_we        : std_logic;
    signal buf_addr        : std_logic_vector(11 downto 0);
    signal buf_rdata       : std_logic_vector(7 downto 0);
    signal buf_wdata       : std_logic_vector(7 downto 0);
    signal buf_en          : std_logic;
    signal buf_we          : std_logic;
begin

    i_mut: entity work.ulpi_host
    port map (
        clock       => clock,
        reset       => reset,
    
        -- Descriptor RAM interface
        descr_addr  => descr_addr,
        descr_rdata => descr_rdata,
        descr_wdata => descr_wdata,
        descr_en    => descr_en,
        descr_we    => descr_we,
    
        -- Buffer RAM interface
        buf_addr    => buf_addr,
        buf_rdata   => buf_rdata,
        buf_wdata   => buf_wdata,
        buf_en      => buf_en,
        buf_we      => buf_we,
    
        -- Transmit Path Interface
        tx_busy     => tx_busy,
        tx_ack      => tx_ack,
        
        -- Interface to send tokens and handshakes
        send_token  => send_token,
        send_handsh => send_handsh,
        tx_pid      => tx_pid,
        tx_token    => tx_token,
        
        -- Interface to send data packets
        send_data   => send_data,
        no_data     => no_data,
        user_data   => user_data,
        user_last   => user_last,
        user_valid  => user_valid,
        user_next   => user_next,
                
        -- Interface to bus initialization unit
        do_reset    => do_reset,
        reset_done  => reset_done,
        speed       => speed,
    
        reset_pkt   => reset_pkt,
        reset_data  => reset_data,
        reset_last  => reset_last,
        reset_valid => reset_valid,
        
        -- Receive Path Interface
        rx_pid          => rx_pid,
        rx_token        => rx_token,
        valid_token     => valid_token,
        valid_handsh    => valid_handsh,
    
        valid_packet    => valid_packet,
        data_valid      => data_valid,
        data_start      => data_start,
        data_out        => data_out,
    
        rx_error        => rx_error );

    i_descr_ram: RAMB16_S36
    port map (
		CLK  => clock,
		SSR  => reset,
		EN   => descr_en,
		WE   => descr_we,
        ADDR => descr_addr,
		DI   => descr_wdata,
		DIP  => X"0",
		DO   => descr_rdata );

    i_buf_ram: RAMB16_S9
    port map (
		CLK  => clock,
		SSR  => reset,
		EN   => buf_en,
		WE   => buf_we,
        ADDR => buf_addr(10 downto 0),
		DI   => buf_wdata,
        DIP  => "0",
		DO   => buf_rdata );
end wrap;
