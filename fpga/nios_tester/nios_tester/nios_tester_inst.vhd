	component nios_tester is
		port (
			audio_in_data           : in  std_logic_vector(31 downto 0) := (others => 'X'); -- data
			audio_in_valid          : in  std_logic                     := 'X';             -- valid
			audio_in_ready          : out std_logic;                                        -- ready
			audio_out_data          : out std_logic_vector(31 downto 0);                    -- data
			audio_out_valid         : out std_logic;                                        -- valid
			audio_out_ready         : in  std_logic                     := 'X';             -- ready
			dummy_export            : in  std_logic                     := 'X';             -- export
			io_ack                  : in  std_logic                     := 'X';             -- ack
			io_rdata                : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
			io_read                 : out std_logic;                                        -- read
			io_wdata                : out std_logic_vector(7 downto 0);                     -- wdata
			io_write                : out std_logic;                                        -- write
			io_address              : out std_logic_vector(19 downto 0);                    -- address
			io_irq                  : in  std_logic                     := 'X';             -- irq
			io_u2p_ack              : in  std_logic                     := 'X';             -- ack
			io_u2p_rdata            : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
			io_u2p_read             : out std_logic;                                        -- read
			io_u2p_wdata            : out std_logic_vector(7 downto 0);                     -- wdata
			io_u2p_write            : out std_logic;                                        -- write
			io_u2p_address          : out std_logic_vector(19 downto 0);                    -- address
			io_u2p_irq              : in  std_logic                     := 'X';             -- irq
			jtag0_jtag_tck          : out std_logic;                                        -- jtag_tck
			jtag0_jtag_tms          : out std_logic;                                        -- jtag_tms
			jtag0_jtag_tdi          : out std_logic;                                        -- jtag_tdi
			jtag0_jtag_tdo          : in  std_logic                     := 'X';             -- jtag_tdo
			jtag1_jtag_tck          : out std_logic;                                        -- jtag_tck
			jtag1_jtag_tms          : out std_logic;                                        -- jtag_tms
			jtag1_jtag_tdi          : out std_logic;                                        -- jtag_tdi
			jtag1_jtag_tdo          : in  std_logic                     := 'X';             -- jtag_tdo
			mem_mem_req_address     : out std_logic_vector(25 downto 0);                    -- mem_req_address
			mem_mem_req_byte_en     : out std_logic_vector(3 downto 0);                     -- mem_req_byte_en
			mem_mem_req_read_writen : out std_logic;                                        -- mem_req_read_writen
			mem_mem_req_request     : out std_logic;                                        -- mem_req_request
			mem_mem_req_tag         : out std_logic_vector(7 downto 0);                     -- mem_req_tag
			mem_mem_req_wdata       : out std_logic_vector(31 downto 0);                    -- mem_req_wdata
			mem_mem_resp_dack_tag   : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_dack_tag
			mem_mem_resp_data       : in  std_logic_vector(31 downto 0) := (others => 'X'); -- mem_resp_data
			mem_mem_resp_rack_tag   : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_rack_tag
			pio_in_port             : in  std_logic_vector(31 downto 0) := (others => 'X'); -- in_port
			pio_out_port            : out std_logic_vector(31 downto 0);                    -- out_port
			spi_MISO                : in  std_logic                     := 'X';             -- MISO
			spi_MOSI                : out std_logic;                                        -- MOSI
			spi_SCLK                : out std_logic;                                        -- SCLK
			spi_SS_n                : out std_logic;                                        -- SS_n
			sys_clock_clk           : in  std_logic                     := 'X';             -- clk
			sys_reset_reset_n       : in  std_logic                     := 'X';             -- reset_n
			uart_rxd                : in  std_logic                     := 'X';             -- rxd
			uart_txd                : out std_logic;                                        -- txd
			uart_cts_n              : in  std_logic                     := 'X';             -- cts_n
			uart_rts_n              : out std_logic;                                        -- rts_n
			jtag_in_data            : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- data
			jtag_in_valid           : in  std_logic                     := 'X';             -- valid
			jtag_in_ready           : out std_logic                                         -- ready
		);
	end component nios_tester;

	u0 : component nios_tester
		port map (
			audio_in_data           => CONNECTED_TO_audio_in_data,           --  audio_in.data
			audio_in_valid          => CONNECTED_TO_audio_in_valid,          --          .valid
			audio_in_ready          => CONNECTED_TO_audio_in_ready,          --          .ready
			audio_out_data          => CONNECTED_TO_audio_out_data,          -- audio_out.data
			audio_out_valid         => CONNECTED_TO_audio_out_valid,         --          .valid
			audio_out_ready         => CONNECTED_TO_audio_out_ready,         --          .ready
			dummy_export            => CONNECTED_TO_dummy_export,            --     dummy.export
			io_ack                  => CONNECTED_TO_io_ack,                  --        io.ack
			io_rdata                => CONNECTED_TO_io_rdata,                --          .rdata
			io_read                 => CONNECTED_TO_io_read,                 --          .read
			io_wdata                => CONNECTED_TO_io_wdata,                --          .wdata
			io_write                => CONNECTED_TO_io_write,                --          .write
			io_address              => CONNECTED_TO_io_address,              --          .address
			io_irq                  => CONNECTED_TO_io_irq,                  --          .irq
			io_u2p_ack              => CONNECTED_TO_io_u2p_ack,              --    io_u2p.ack
			io_u2p_rdata            => CONNECTED_TO_io_u2p_rdata,            --          .rdata
			io_u2p_read             => CONNECTED_TO_io_u2p_read,             --          .read
			io_u2p_wdata            => CONNECTED_TO_io_u2p_wdata,            --          .wdata
			io_u2p_write            => CONNECTED_TO_io_u2p_write,            --          .write
			io_u2p_address          => CONNECTED_TO_io_u2p_address,          --          .address
			io_u2p_irq              => CONNECTED_TO_io_u2p_irq,              --          .irq
			jtag0_jtag_tck          => CONNECTED_TO_jtag0_jtag_tck,          --     jtag0.jtag_tck
			jtag0_jtag_tms          => CONNECTED_TO_jtag0_jtag_tms,          --          .jtag_tms
			jtag0_jtag_tdi          => CONNECTED_TO_jtag0_jtag_tdi,          --          .jtag_tdi
			jtag0_jtag_tdo          => CONNECTED_TO_jtag0_jtag_tdo,          --          .jtag_tdo
			jtag1_jtag_tck          => CONNECTED_TO_jtag1_jtag_tck,          --     jtag1.jtag_tck
			jtag1_jtag_tms          => CONNECTED_TO_jtag1_jtag_tms,          --          .jtag_tms
			jtag1_jtag_tdi          => CONNECTED_TO_jtag1_jtag_tdi,          --          .jtag_tdi
			jtag1_jtag_tdo          => CONNECTED_TO_jtag1_jtag_tdo,          --          .jtag_tdo
			mem_mem_req_address     => CONNECTED_TO_mem_mem_req_address,     --       mem.mem_req_address
			mem_mem_req_byte_en     => CONNECTED_TO_mem_mem_req_byte_en,     --          .mem_req_byte_en
			mem_mem_req_read_writen => CONNECTED_TO_mem_mem_req_read_writen, --          .mem_req_read_writen
			mem_mem_req_request     => CONNECTED_TO_mem_mem_req_request,     --          .mem_req_request
			mem_mem_req_tag         => CONNECTED_TO_mem_mem_req_tag,         --          .mem_req_tag
			mem_mem_req_wdata       => CONNECTED_TO_mem_mem_req_wdata,       --          .mem_req_wdata
			mem_mem_resp_dack_tag   => CONNECTED_TO_mem_mem_resp_dack_tag,   --          .mem_resp_dack_tag
			mem_mem_resp_data       => CONNECTED_TO_mem_mem_resp_data,       --          .mem_resp_data
			mem_mem_resp_rack_tag   => CONNECTED_TO_mem_mem_resp_rack_tag,   --          .mem_resp_rack_tag
			pio_in_port             => CONNECTED_TO_pio_in_port,             --       pio.in_port
			pio_out_port            => CONNECTED_TO_pio_out_port,            --          .out_port
			spi_MISO                => CONNECTED_TO_spi_MISO,                --       spi.MISO
			spi_MOSI                => CONNECTED_TO_spi_MOSI,                --          .MOSI
			spi_SCLK                => CONNECTED_TO_spi_SCLK,                --          .SCLK
			spi_SS_n                => CONNECTED_TO_spi_SS_n,                --          .SS_n
			sys_clock_clk           => CONNECTED_TO_sys_clock_clk,           -- sys_clock.clk
			sys_reset_reset_n       => CONNECTED_TO_sys_reset_reset_n,       -- sys_reset.reset_n
			uart_rxd                => CONNECTED_TO_uart_rxd,                --      uart.rxd
			uart_txd                => CONNECTED_TO_uart_txd,                --          .txd
			uart_cts_n              => CONNECTED_TO_uart_cts_n,              --          .cts_n
			uart_rts_n              => CONNECTED_TO_uart_rts_n,              --          .rts_n
			jtag_in_data            => CONNECTED_TO_jtag_in_data,            --   jtag_in.data
			jtag_in_valid           => CONNECTED_TO_jtag_in_valid,           --          .valid
			jtag_in_ready           => CONNECTED_TO_jtag_in_ready            --          .ready
		);

