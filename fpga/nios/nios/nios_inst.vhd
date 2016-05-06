	component nios is
		port (
			altmemddr_0_auxfull_clk        : out   std_logic;                                        -- clk
			altmemddr_0_auxhalf_clk        : out   std_logic;                                        -- clk
			clk50_clk                      : in    std_logic                     := 'X';             -- clk
			io_ack                         : in    std_logic                     := 'X';             -- ack
			io_rdata                       : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
			io_read                        : out   std_logic;                                        -- read
			io_wdata                       : out   std_logic_vector(7 downto 0);                     -- wdata
			io_write                       : out   std_logic;                                        -- write
			io_address                     : out   std_logic_vector(19 downto 0);                    -- address
			io_irq                         : in    std_logic                     := 'X';             -- irq
			mem32_address                  : in    std_logic_vector(25 downto 0) := (others => 'X'); -- address
			mem32_direction                : in    std_logic                     := 'X';             -- direction
			mem32_byte_en                  : in    std_logic_vector(3 downto 0)  := (others => 'X'); -- byte_en
			mem32_wdata                    : in    std_logic_vector(31 downto 0) := (others => 'X'); -- wdata
			mem32_request                  : in    std_logic                     := 'X';             -- request
			mem32_tag                      : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- tag
			mem32_dack_tag                 : out   std_logic_vector(7 downto 0);                     -- dack_tag
			mem32_rdata                    : out   std_logic_vector(31 downto 0);                    -- rdata
			mem32_rack                     : out   std_logic;                                        -- rack
			mem32_rack_tag                 : out   std_logic_vector(7 downto 0);                     -- rack_tag
			mem_external_local_refresh_ack : out   std_logic;                                        -- local_refresh_ack
			mem_external_local_init_done   : out   std_logic;                                        -- local_init_done
			mem_external_reset_phy_clk_n   : out   std_logic;                                        -- reset_phy_clk_n
			memory_mem_odt                 : out   std_logic_vector(0 downto 0);                     -- mem_odt
			memory_mem_clk                 : inout std_logic_vector(0 downto 0)  := (others => 'X'); -- mem_clk
			memory_mem_clk_n               : inout std_logic_vector(0 downto 0)  := (others => 'X'); -- mem_clk_n
			memory_mem_cs_n                : out   std_logic_vector(0 downto 0);                     -- mem_cs_n
			memory_mem_cke                 : out   std_logic_vector(0 downto 0);                     -- mem_cke
			memory_mem_addr                : out   std_logic_vector(13 downto 0);                    -- mem_addr
			memory_mem_ba                  : out   std_logic_vector(1 downto 0);                     -- mem_ba
			memory_mem_ras_n               : out   std_logic;                                        -- mem_ras_n
			memory_mem_cas_n               : out   std_logic;                                        -- mem_cas_n
			memory_mem_we_n                : out   std_logic;                                        -- mem_we_n
			memory_mem_dq                  : inout std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_dq
			memory_mem_dqs                 : inout std_logic_vector(0 downto 0)  := (others => 'X'); -- mem_dqs
			memory_mem_dm                  : out   std_logic_vector(0 downto 0);                     -- mem_dm
			pio_in_port                    : in    std_logic_vector(31 downto 0) := (others => 'X'); -- in_port
			pio_out_port                   : out   std_logic_vector(31 downto 0);                    -- out_port
			reset_reset_n                  : in    std_logic                     := 'X';             -- reset_n
			sys_clock_clk                  : out   std_logic;                                        -- clk
			sys_reset_reset_n              : out   std_logic                                         -- reset_n
		);
	end component nios;

	u0 : component nios
		port map (
			altmemddr_0_auxfull_clk        => CONNECTED_TO_altmemddr_0_auxfull_clk,        -- altmemddr_0_auxfull.clk
			altmemddr_0_auxhalf_clk        => CONNECTED_TO_altmemddr_0_auxhalf_clk,        -- altmemddr_0_auxhalf.clk
			clk50_clk                      => CONNECTED_TO_clk50_clk,                      --               clk50.clk
			io_ack                         => CONNECTED_TO_io_ack,                         --                  io.ack
			io_rdata                       => CONNECTED_TO_io_rdata,                       --                    .rdata
			io_read                        => CONNECTED_TO_io_read,                        --                    .read
			io_wdata                       => CONNECTED_TO_io_wdata,                       --                    .wdata
			io_write                       => CONNECTED_TO_io_write,                       --                    .write
			io_address                     => CONNECTED_TO_io_address,                     --                    .address
			io_irq                         => CONNECTED_TO_io_irq,                         --                    .irq
			mem32_address                  => CONNECTED_TO_mem32_address,                  --               mem32.address
			mem32_direction                => CONNECTED_TO_mem32_direction,                --                    .direction
			mem32_byte_en                  => CONNECTED_TO_mem32_byte_en,                  --                    .byte_en
			mem32_wdata                    => CONNECTED_TO_mem32_wdata,                    --                    .wdata
			mem32_request                  => CONNECTED_TO_mem32_request,                  --                    .request
			mem32_tag                      => CONNECTED_TO_mem32_tag,                      --                    .tag
			mem32_dack_tag                 => CONNECTED_TO_mem32_dack_tag,                 --                    .dack_tag
			mem32_rdata                    => CONNECTED_TO_mem32_rdata,                    --                    .rdata
			mem32_rack                     => CONNECTED_TO_mem32_rack,                     --                    .rack
			mem32_rack_tag                 => CONNECTED_TO_mem32_rack_tag,                 --                    .rack_tag
			mem_external_local_refresh_ack => CONNECTED_TO_mem_external_local_refresh_ack, --        mem_external.local_refresh_ack
			mem_external_local_init_done   => CONNECTED_TO_mem_external_local_init_done,   --                    .local_init_done
			mem_external_reset_phy_clk_n   => CONNECTED_TO_mem_external_reset_phy_clk_n,   --                    .reset_phy_clk_n
			memory_mem_odt                 => CONNECTED_TO_memory_mem_odt,                 --              memory.mem_odt
			memory_mem_clk                 => CONNECTED_TO_memory_mem_clk,                 --                    .mem_clk
			memory_mem_clk_n               => CONNECTED_TO_memory_mem_clk_n,               --                    .mem_clk_n
			memory_mem_cs_n                => CONNECTED_TO_memory_mem_cs_n,                --                    .mem_cs_n
			memory_mem_cke                 => CONNECTED_TO_memory_mem_cke,                 --                    .mem_cke
			memory_mem_addr                => CONNECTED_TO_memory_mem_addr,                --                    .mem_addr
			memory_mem_ba                  => CONNECTED_TO_memory_mem_ba,                  --                    .mem_ba
			memory_mem_ras_n               => CONNECTED_TO_memory_mem_ras_n,               --                    .mem_ras_n
			memory_mem_cas_n               => CONNECTED_TO_memory_mem_cas_n,               --                    .mem_cas_n
			memory_mem_we_n                => CONNECTED_TO_memory_mem_we_n,                --                    .mem_we_n
			memory_mem_dq                  => CONNECTED_TO_memory_mem_dq,                  --                    .mem_dq
			memory_mem_dqs                 => CONNECTED_TO_memory_mem_dqs,                 --                    .mem_dqs
			memory_mem_dm                  => CONNECTED_TO_memory_mem_dm,                  --                    .mem_dm
			pio_in_port                    => CONNECTED_TO_pio_in_port,                    --                 pio.in_port
			pio_out_port                   => CONNECTED_TO_pio_out_port,                   --                    .out_port
			reset_reset_n                  => CONNECTED_TO_reset_reset_n,                  --               reset.reset_n
			sys_clock_clk                  => CONNECTED_TO_sys_clock_clk,                  --           sys_clock.clk
			sys_reset_reset_n              => CONNECTED_TO_sys_reset_reset_n               --           sys_reset.reset_n
		);

