library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity usb_host is
port (
    ulpi_clock  : in  std_logic;
    ulpi_reset  : in  std_logic;

    -- ULPI Interface
    ULPI_DATA   : inout std_logic_vector(7 downto 0);
    ULPI_DIR    : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;

    -- register interface bus
    sys_clock   : in    std_logic;
    sys_reset   : in    std_logic;
    
    sys_address : in    std_logic_vector(12 downto 0); -- 8K block
    sys_write   : in    std_logic;
    sys_request : in    std_logic;
    sys_wdata   : in    std_logic_vector(7 downto 0);
    sys_rdata   : out   std_logic_vector(7 downto 0);
    sys_rack    : out   std_logic;
    sys_dack    : out   std_logic );

--    -- Interface to read/write registers
--    read_reg    : in  std_logic;
--    write_reg   : in  std_logic;
--    reg_ack     : out std_logic;
--    address     : in  std_logic_vector(5 downto 0);
--    write_data  : in  std_logic_vector(7 downto 0);
--    read_data   : out std_logic_vector(7 downto 0) );
--        
end usb_host;

architecture wrap of usb_host is
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
    
    signal tx_busy         : std_logic;
    signal tx_ack          : std_logic;
    signal send_token      : std_logic;
    signal send_handsh     : std_logic;
    signal tx_pid          : std_logic_vector(3 downto 0);
    signal tx_token        : std_logic_vector(10 downto 0);
    signal send_data       : std_logic;
    signal no_data         : std_logic;
    signal user_data       : std_logic_vector(7 downto 0);
    signal user_last       : std_logic;
    signal user_valid      : std_logic;
    signal user_next       : std_logic;

    signal rx_pid          : std_logic_vector(3 downto 0) := X"0";
    signal rx_token        : std_logic_vector(10 downto 0) := (others => '0');
    signal valid_token     : std_logic := '0';
    signal valid_handsh    : std_logic := '0';
    signal valid_packet    : std_logic := '0';
    signal data_valid      : std_logic := '0';
    signal data_start      : std_logic := '0';
    signal data_out        : std_logic_vector(7 downto 0) := X"12";
    signal rx_error        : std_logic := '0';
    
    signal tx_data         : std_logic_vector(7 downto 0) := X"00";
    signal tx_last         : std_logic := '0';
    signal tx_valid        : std_logic := '0';
    signal tx_start        : std_logic := '0';
    signal tx_next         : std_logic := '0';
    signal rx_data         : std_logic_vector(7 downto 0);
    signal status          : std_logic_vector(7 downto 0);
    signal rx_last         : std_logic;
    signal rx_valid        : std_logic;
    signal rx_store        : std_logic;
    signal rx_register     : std_logic;

    signal read_reg        : std_logic := '0';
    signal write_reg       : std_logic;
    signal reg_ack         : std_logic;
    signal address         : std_logic_vector(5 downto 0);  
    signal write_data      : std_logic_vector(7 downto 0);  
    signal read_data       : std_logic_vector(7 downto 0);

    signal reset_pkt       : std_logic;
    signal reset_valid     : std_logic;
    signal reset_last      : std_logic;
    signal reset_data      : std_logic_vector(7 downto 0);

    signal power_en        : std_logic;
    signal do_reset        : std_logic;
    signal reset_done      : std_logic;
    signal speed           : std_logic_vector(1 downto 0);

    signal sys_buf_en       : std_logic;
    signal sys_descr_en     : std_logic;
    signal sys_sel_d        : std_logic;
    signal sys_buf_rdata    : std_logic_vector(7 downto 0);
    signal sys_descr_rdata  : std_logic_vector(7 downto 0);

begin                             
                                  
    i_host: entity work.ulpi_host 
    port map (                    
        clock       => ulpi_clock,
        reset       => ulpi_reset,
    
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
                
        -- Interface to bus reset unit
        power_en    => power_en,
        do_reset    => do_reset,
        reset_done  => reset_done,
        speed       => speed,
        
        reset_pkt   => reset_pkt,
        reset_data  => reset_data,
        reset_last  => reset_last,
        reset_valid => reset_valid,
        
        -- Interface to read/write registers
--        read_reg    => read_reg,
--        write_reg   => write_reg,
--        address     => address,
--        write_data  => write_data,
--        read_data   => X"55", -- read_data
        
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

    i_descr_ram: RAMB16_S9_S36
    port map (
		CLKA  => sys_clock,
		SSRA  => sys_reset,
		ENA   => sys_descr_en,
		WEA   => sys_write,
        ADDRA => sys_address(10 downto 0),
		DIA   => sys_wdata,
		DIPA  => "0",
		DOA   => sys_descr_rdata,

		CLKB  => ulpi_clock,
		SSRB  => ulpi_reset,
		ENB   => descr_en,
		WEB   => descr_we,
        ADDRB => descr_addr,
		DIB   => descr_wdata,
		DIPB  => X"0",
		DOB   => descr_rdata );

    i_buf_ram: RAMB16_S9_S9
    port map (
		CLKA  => sys_clock,
		SSRA  => sys_reset,
		ENA   => sys_buf_en,
		WEA   => sys_write,
        ADDRA => sys_address(10 downto 0),
		DIA   => sys_wdata,
		DIPA  => "0",
		DOA   => sys_buf_rdata,
		
		CLKB  => ulpi_clock,
		SSRB  => ulpi_reset,
		ENB   => buf_en,
		WEB   => buf_we,
        ADDRB => buf_addr(10 downto 0),
		DIB   => buf_wdata,
        DIPB  => "0",
		DOB   => buf_rdata );

		
    sys_buf_en   <= sys_request and sys_address(12);
    sys_descr_en <= sys_request and not sys_address(12);
    
    sys_rack <= sys_request;
    
    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            sys_dack <= sys_request;
            sys_sel_d <= sys_address(12);
        end if;
    end process;

    sys_rdata <= sys_buf_rdata when sys_sel_d='1' else sys_descr_rdata;
    

    i_tx: entity work.ulpi_tx
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        -- Bus Interface
        tx_start    => tx_start,
        tx_last     => tx_last,
        tx_valid    => tx_valid,
        tx_next     => tx_next,
        tx_data     => tx_data,
        rx_register => rx_register,
        rx_data     => rx_data,
        
        -- Status
        busy        => tx_busy,
        tx_ack      => tx_ack,         
        reg_ack     => reg_ack,
        
        -- Interface to send tokens
        send_token  => send_token,
        send_handsh => send_handsh,
        pid         => tx_pid,
        token       => tx_token,
        
        -- Interface to send data packets
        send_data   => send_data,
        user_data   => user_data,
        user_last   => user_last,
        user_valid  => user_valid,
        user_next   => user_next,
                
        -- Interface to read/write registers
        read_reg    => read_reg,
        write_reg   => write_reg,
        address     => address,
        write_data  => write_data,
        read_data   => read_data );

    i_rx: entity work.ulpi_rx
    generic map (
        g_allow_token   => false )
    port map (
        clock           => ulpi_clock,
        reset           => ulpi_reset,
                        
        rx_data         => rx_data,
        rx_last         => rx_last,
        rx_valid        => rx_valid,
        rx_store        => rx_store,
                        
        pid             => rx_pid,
        token           => rx_token,
        valid_token     => valid_token,
    
        valid_packet    => valid_packet,
        data_out        => data_out,
        data_valid      => data_valid,
        data_start      => data_start,
        
        error           => rx_error );

    i_bus: entity work.ulpi_bus
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP,
        
        status      => status,
        
        -- stream interface
        tx_data     => tx_data,
        tx_last     => tx_last,
        tx_valid    => tx_valid,
        tx_start    => tx_start,
        tx_next     => tx_next,
    
        rx_data     => rx_data,
        rx_last     => rx_last,
        rx_register => rx_register,
        rx_store    => rx_store,
        rx_valid    => rx_valid );

    i_reset: entity work.bus_reset
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        do_reset    => do_reset,
        power_en    => power_en,
    
        reset_done  => reset_done,
        speed       => speed,
        
        -- status
        status      => status,
    
        -- register interface
        write_reg   => write_reg,
        write_data  => write_data,
        address     => address,
        reg_ack     => reg_ack,
        
        send_packet => reset_pkt,
        user_data   => reset_data,
        user_last   => reset_last,
        user_valid  => reset_valid );
		
end wrap;
