library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;
use work.io_bus_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity usb_host_io is
generic (
    g_simulation    : boolean := false );
port (
    ulpi_clock  : in  std_logic;
    ulpi_reset  : in  std_logic;

    -- ULPI Interface
    ULPI_DATA   : inout std_logic_vector(7 downto 0);
    ULPI_DIR    : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;

	-- LED interface
	usb_busy	: out   std_logic;
	
    -- register interface bus
    sys_clock   : in    std_logic;
    sys_reset   : in    std_logic;
    
    sys_io_req  : in    t_io_req;
    sys_io_resp : out   t_io_resp );

end usb_host_io;

architecture wrap of usb_host_io is

    signal descr_addr      : std_logic_vector(8 downto 0);
    signal descr_rdata     : std_logic_vector(31 downto 0);
    signal descr_wdata     : std_logic_vector(31 downto 0);
    signal descr_en        : std_logic;
    signal descr_we        : std_logic;
    signal buf_addr        : std_logic_vector(10 downto 0);
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

    signal reg_read        : std_logic := '0';
    signal reg_write       : std_logic;
    signal reg_ack         : std_logic;
    signal reg_addr        : std_logic_vector(5 downto 0);  
    signal reg_wdata       : std_logic_vector(7 downto 0);  

--    signal reset_pkt       : std_logic;
--    signal reset_valid     : std_logic;
--    signal reset_last      : std_logic;
--    signal reset_data      : std_logic_vector(7 downto 0);

    signal send_reset_data : std_logic;
    signal reset_last      : std_logic;
    signal reset_data      : std_logic_vector(7 downto 0);
    
    signal reset_done      : std_logic;
	signal sof_enable	   : std_logic;
	signal scan_enable     : std_logic;
    signal speed           : std_logic_vector(1 downto 0);
    signal gap_length      : std_logic_vector(7 downto 0);
    signal abort           : std_logic;

    signal sys_addr_i       : std_logic_vector(sys_io_req.address'range);
    signal sys_buf_en       : std_logic;
    signal sys_descr_en     : std_logic;
    signal sys_sel_d        : std_logic_vector(2 downto 0);
    signal sys_buf_rdata    : std_logic_vector(7 downto 0);
    signal sys_descr_rdata  : std_logic_vector(7 downto 0);

    signal sys_cmd_read     : std_logic;
    signal sys_cmd_write    : std_logic;
    signal sys_cmd_rdata    : std_logic_vector(7 downto 0);

    signal sys_cmd_full     : std_logic;
    signal sys_cmd_count    : std_logic_vector(2 downto 0);    
    signal sys_resp_get     : std_logic;
    signal sys_resp_data    : std_logic_vector(8 downto 0);
    signal sys_resp_empty   : std_logic;
        
    signal cmd_get          : std_logic;
    signal cmd_empty        : std_logic;
    signal cmd_data         : std_logic_vector(7 downto 0);
                            
    signal resp_put         : std_logic;
    signal resp_full        : std_logic;
    signal resp_data        : std_logic_vector(8 downto 0);
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
        reset_done  => reset_done,
		sof_enable  => sof_enable,
		scan_enable => scan_enable,
        speed       => speed,
        abort       => abort,
                
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
		WEA   => sys_io_req.write,
        ADDRA => sys_addr_i(10 downto 0),
		DIA   => sys_io_req.data,
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
		WEA   => sys_io_req.write,
        ADDRA => sys_addr_i(10 downto 0),
		DIA   => sys_io_req.data,
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
        rx_data     => rx_data,
        
        -- Status
        speed       => speed,
        status      => status,
        gap_length  => gap_length,
        busy        => tx_busy,
        tx_ack      => tx_ack,         
        
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
                
        -- Interface to read/write registers and reset packets
        send_reset_data => send_reset_data,
        reset_data      => reset_data,
        reset_last      => reset_last );
        

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
        valid_handsh    => valid_handsh,
    
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
        
        -- register interface
        reg_read    => reg_read,
        reg_write   => reg_write,
        reg_address => reg_addr,
        reg_wdata   => reg_wdata,
        reg_ack     => reg_ack,

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
    generic map (
        g_simulation => g_simulation )
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        reset_done  => reset_done,
		sof_enable  => sof_enable,
		scan_enable => scan_enable,
        speed       => speed,
        abort       => abort,
        
        -- Command / response interface
        cmd_get     => cmd_get,
        cmd_empty   => cmd_empty,
        cmd_data    => cmd_data,
                                
        resp_put    => resp_put,
        resp_full   => resp_full,
        resp_data   => resp_data,

        -- status
        status      => status,
        gap_length  => gap_length,
    	usb_busy	=> usb_busy,
    	
        -- register interface
        reg_read    => reg_read,
        reg_write   => reg_write,
        reg_rdata   => rx_data,
        reg_wdata   => reg_wdata,
        reg_address => reg_addr,
        reg_ack     => reg_ack,
        
        -- interface to packet transmitter
        send_packet => send_reset_data,
        user_data   => reset_data,
        user_last   => reset_last,
        user_valid  => open );
		
    i_cmd_fifo: entity work.async_fifo
    generic map (
        g_data_width => 8,
        g_depth_bits => 3,
        g_count_bits => 3,
        g_threshold  => 3,
        g_storage    => "distributed" )
    port map (
        -- write port signals (synchronized to write clock)
        wr_clock       => sys_clock,
        wr_reset       => sys_reset,
        wr_en          => sys_cmd_write,
        wr_din         => sys_io_req.data,
        wr_flush       => '0',
        wr_count       => sys_cmd_count,
        wr_full        => open,
        wr_almost_full => sys_cmd_full,
        wr_error       => open,
        wr_inhibit     => open,

        -- read port signals (synchronized to read clock)
        rd_clock        => ulpi_clock,
        rd_reset        => ulpi_reset,
        rd_en           => cmd_get,
        rd_dout         => cmd_data,
        rd_count        => open,
        rd_empty        => cmd_empty,
        rd_almost_empty => open,
        rd_error        => open );

    i_resp_fifo: entity work.async_fifo
    generic map (
        g_data_width => 9,
        g_depth_bits => 3,
        g_count_bits => 3,
        g_threshold  => 3,
        g_storage    => "distributed" )
    port map (
        -- write port signals (synchronized to write clock)
        wr_clock       => ulpi_clock,
        wr_reset       => ulpi_reset,
        wr_en          => resp_put,
        wr_din         => resp_data,
        wr_flush       => '0',
        wr_count       => open,
        wr_full        => resp_full,
        wr_almost_full => open,
        wr_error       => open,
        wr_inhibit     => open,

        -- read port signals (synchronized to read clock)
        rd_clock        => sys_clock,
        rd_reset        => sys_reset,
        rd_en           => sys_resp_get,
        rd_dout         => sys_resp_data,
        rd_count        => open,
        rd_empty        => sys_resp_empty,
        rd_almost_empty => open,
        rd_error        => open );

    -- BUS INTERFACE --
    -- command / response output word generator
    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            sys_resp_get <= '0';
            case sys_io_req.address(1 downto 0) is
            when "00" =>
                sys_cmd_rdata <= sys_resp_data(7 downto 0);
            when "01" =>
                sys_cmd_rdata <= not sys_resp_empty & "000000" & sys_resp_data(8);
            when "10" =>
                sys_cmd_rdata <= sys_cmd_full & "0000" & sys_cmd_count;
            when "11" =>
                sys_cmd_rdata <= X"00";
                sys_resp_get  <= sys_cmd_read; -- if reading, we'll pull one
            when others =>
                null;
            end case;
        end if;
    end process;

    sys_addr_i(sys_addr_i'high downto 0) <= std_logic_vector(sys_io_req.address(sys_addr_i'range));
    
    sys_buf_en    <= (sys_io_req.read or sys_io_req.write) and sys_io_req.address(12);
    sys_descr_en  <= (sys_io_req.read or sys_io_req.write) and not sys_io_req.address(12) and not sys_io_req.address(11);
    sys_cmd_read  <= sys_io_req.read and not sys_io_req.address(12) and sys_io_req.address(11);
    sys_cmd_write <= sys_io_req.write and not sys_io_req.address(12) and sys_io_req.address(11);
        
    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            sys_io_resp.ack <= sys_io_req.read or sys_io_req.write;
            sys_sel_d <= sys_io_req.read & std_logic_vector(sys_io_req.address(12 downto 11));
        end if;
    end process;

    with sys_sel_d select sys_io_resp.data <=
        sys_buf_rdata when "110" | "111",
        sys_descr_rdata when "100",
        sys_cmd_rdata when "101",
        X"00" when others;

end wrap;
