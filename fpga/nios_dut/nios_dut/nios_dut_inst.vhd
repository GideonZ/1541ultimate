	component nios_dut is
		port (
			audio_in_data            : in  std_logic_vector(31 downto 0) := (others => 'X'); -- data
			audio_in_valid           : in  std_logic                     := 'X';             -- valid
			audio_in_ready           : out std_logic;                                        -- ready
			audio_out_data           : out std_logic_vector(31 downto 0);                    -- data
			audio_out_valid          : out std_logic;                                        -- valid
			audio_out_ready          : in  std_logic                     := 'X';             -- ready
			dummy_export             : in  std_logic                     := 'X';             -- export
			io_ack                   : in  std_logic                     := 'X';             -- ack
			io_rdata                 : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
			io_read                  : out std_logic;                                        -- read
			io_wdata                 : out std_logic_vector(7 downto 0);                     -- wdata
			io_write                 : out std_logic;                                        -- write
			io_address               : out std_logic_vector(19 downto 0);                    -- address
			io_irq                   : in  std_logic                     := 'X';             -- irq
			io_u2p_ack               : in  std_logic                     := 'X';             -- ack
			io_u2p_rdata             : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
			io_u2p_read              : out std_logic;                                        -- read
			io_u2p_wdata             : out std_logic_vector(7 downto 0);                     -- wdata
			io_u2p_write             : out std_logic;                                        -- write
			io_u2p_address           : out std_logic_vector(19 downto 0);                    -- address
			io_u2p_irq               : in  std_logic                     := 'X';             -- irq
			jtag_io_input_vector     : in  std_logic_vector(47 downto 0) := (others => 'X'); -- input_vector
			jtag_io_output_vector    : out std_logic_vector(7 downto 0);                     -- output_vector
			jtag_test_clocks_clock_1 : in  std_logic                     := 'X';             -- clock_1
			jtag_test_clocks_clock_2 : in  std_logic                     := 'X';             -- clock_2
			mem_mem_req_address      : out std_logic_vector(25 downto 0);                    -- mem_req_address
			mem_mem_req_byte_en      : out std_logic_vector(3 downto 0);                     -- mem_req_byte_en
			mem_mem_req_read_writen  : out std_logic;                                        -- mem_req_read_writen
			mem_mem_req_request      : out std_logic;                                        -- mem_req_request
			mem_mem_req_tag          : out std_logic_vector(7 downto 0);                     -- mem_req_tag
			mem_mem_req_wdata        : out std_logic_vector(31 downto 0);                    -- mem_req_wdata
			mem_mem_resp_dack_tag    : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_dack_tag
			mem_mem_resp_data        : in  std_logic_vector(31 downto 0) := (others => 'X'); -- mem_resp_data
			mem_mem_resp_rack_tag    : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_rack_tag
			pio1_export              : in  std_logic_vector(31 downto 0) := (others => 'X'); -- export
			pio2_export              : in  std_logic_vector(19 downto 0) := (others => 'X'); -- export
			pio3_export              : out std_logic_vector(7 downto 0);                     -- export
			sys_clock_clk            : in  std_logic                     := 'X';             -- clk
			sys_reset_reset_n        : in  std_logic                     := 'X';             -- reset_n
			uart_rxd                 : in  std_logic                     := 'X';             -- rxd
			uart_txd                 : out std_logic;                                        -- txd
			uart_cts_n               : in  std_logic                     := 'X';             -- cts_n
			uart_rts_n               : out std_logic                                         -- rts_n
		);
	end component nios_dut;

	u0 : component nios_dut
		port map (
			audio_in_data            => CONNECTED_TO_audio_in_data,            --         audio_in.data
			audio_in_valid           => CONNECTED_TO_audio_in_valid,           --                 .valid
			audio_in_ready           => CONNECTED_TO_audio_in_ready,           --                 .ready
			audio_out_data           => CONNECTED_TO_audio_out_data,           --        audio_out.data
			audio_out_valid          => CONNECTED_TO_audio_out_valid,          --                 .valid
			audio_out_ready          => CONNECTED_TO_audio_out_ready,          --                 .ready
			dummy_export             => CONNECTED_TO_dummy_export,             --            dummy.export
			io_ack                   => CONNECTED_TO_io_ack,                   --               io.ack
			io_rdata                 => CONNECTED_TO_io_rdata,                 --                 .rdata
			io_read                  => CONNECTED_TO_io_read,                  --                 .read
			io_wdata                 => CONNECTED_TO_io_wdata,                 --                 .wdata
			io_write                 => CONNECTED_TO_io_write,                 --                 .write
			io_address               => CONNECTED_TO_io_address,               --                 .address
			io_irq                   => CONNECTED_TO_io_irq,                   --                 .irq
			io_u2p_ack               => CONNECTED_TO_io_u2p_ack,               --           io_u2p.ack
			io_u2p_rdata             => CONNECTED_TO_io_u2p_rdata,             --                 .rdata
			io_u2p_read              => CONNECTED_TO_io_u2p_read,              --                 .read
			io_u2p_wdata             => CONNECTED_TO_io_u2p_wdata,             --                 .wdata
			io_u2p_write             => CONNECTED_TO_io_u2p_write,             --                 .write
			io_u2p_address           => CONNECTED_TO_io_u2p_address,           --                 .address
			io_u2p_irq               => CONNECTED_TO_io_u2p_irq,               --                 .irq
			jtag_io_input_vector     => CONNECTED_TO_jtag_io_input_vector,     --          jtag_io.input_vector
			jtag_io_output_vector    => CONNECTED_TO_jtag_io_output_vector,    --                 .output_vector
			jtag_test_clocks_clock_1 => CONNECTED_TO_jtag_test_clocks_clock_1, -- jtag_test_clocks.clock_1
			jtag_test_clocks_clock_2 => CONNECTED_TO_jtag_test_clocks_clock_2, --                 .clock_2
			mem_mem_req_address      => CONNECTED_TO_mem_mem_req_address,      --              mem.mem_req_address
			mem_mem_req_byte_en      => CONNECTED_TO_mem_mem_req_byte_en,      --                 .mem_req_byte_en
			mem_mem_req_read_writen  => CONNECTED_TO_mem_mem_req_read_writen,  --                 .mem_req_read_writen
			mem_mem_req_request      => CONNECTED_TO_mem_mem_req_request,      --                 .mem_req_request
			mem_mem_req_tag          => CONNECTED_TO_mem_mem_req_tag,          --                 .mem_req_tag
			mem_mem_req_wdata        => CONNECTED_TO_mem_mem_req_wdata,        --                 .mem_req_wdata
			mem_mem_resp_dack_tag    => CONNECTED_TO_mem_mem_resp_dack_tag,    --                 .mem_resp_dack_tag
			mem_mem_resp_data        => CONNECTED_TO_mem_mem_resp_data,        --                 .mem_resp_data
			mem_mem_resp_rack_tag    => CONNECTED_TO_mem_mem_resp_rack_tag,    --                 .mem_resp_rack_tag
			pio1_export              => CONNECTED_TO_pio1_export,              --             pio1.export
			pio2_export              => CONNECTED_TO_pio2_export,              --             pio2.export
			pio3_export              => CONNECTED_TO_pio3_export,              --             pio3.export
			sys_clock_clk            => CONNECTED_TO_sys_clock_clk,            --        sys_clock.clk
			sys_reset_reset_n        => CONNECTED_TO_sys_reset_reset_n,        --        sys_reset.reset_n
			uart_rxd                 => CONNECTED_TO_uart_rxd,                 --             uart.rxd
			uart_txd                 => CONNECTED_TO_uart_txd,                 --                 .txd
			uart_cts_n               => CONNECTED_TO_uart_cts_n,               --                 .cts_n
			uart_rts_n               => CONNECTED_TO_uart_rts_n                --                 .rts_n
		);

