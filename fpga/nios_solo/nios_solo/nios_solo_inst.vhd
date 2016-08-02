	component nios_solo is
		port (
			clk_clk                 : in  std_logic                     := 'X';             -- clk
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
			mem_mem_req_address     : out std_logic_vector(25 downto 0);                    -- mem_req_address
			mem_mem_req_byte_en     : out std_logic_vector(3 downto 0);                     -- mem_req_byte_en
			mem_mem_req_read_writen : out std_logic;                                        -- mem_req_read_writen
			mem_mem_req_request     : out std_logic;                                        -- mem_req_request
			mem_mem_req_tag         : out std_logic_vector(7 downto 0);                     -- mem_req_tag
			mem_mem_req_wdata       : out std_logic_vector(31 downto 0);                    -- mem_req_wdata
			mem_mem_resp_dack_tag   : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_dack_tag
			mem_mem_resp_data       : in  std_logic_vector(31 downto 0) := (others => 'X'); -- mem_resp_data
			mem_mem_resp_rack_tag   : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_rack_tag
			reset_reset_n           : in  std_logic                     := 'X';             -- reset_n
			dummy_export            : in  std_logic                     := 'X'              -- export
		);
	end component nios_solo;

	u0 : component nios_solo
		port map (
			clk_clk                 => CONNECTED_TO_clk_clk,                 --    clk.clk
			io_ack                  => CONNECTED_TO_io_ack,                  --     io.ack
			io_rdata                => CONNECTED_TO_io_rdata,                --       .rdata
			io_read                 => CONNECTED_TO_io_read,                 --       .read
			io_wdata                => CONNECTED_TO_io_wdata,                --       .wdata
			io_write                => CONNECTED_TO_io_write,                --       .write
			io_address              => CONNECTED_TO_io_address,              --       .address
			io_irq                  => CONNECTED_TO_io_irq,                  --       .irq
			io_u2p_ack              => CONNECTED_TO_io_u2p_ack,              -- io_u2p.ack
			io_u2p_rdata            => CONNECTED_TO_io_u2p_rdata,            --       .rdata
			io_u2p_read             => CONNECTED_TO_io_u2p_read,             --       .read
			io_u2p_wdata            => CONNECTED_TO_io_u2p_wdata,            --       .wdata
			io_u2p_write            => CONNECTED_TO_io_u2p_write,            --       .write
			io_u2p_address          => CONNECTED_TO_io_u2p_address,          --       .address
			io_u2p_irq              => CONNECTED_TO_io_u2p_irq,              --       .irq
			mem_mem_req_address     => CONNECTED_TO_mem_mem_req_address,     --    mem.mem_req_address
			mem_mem_req_byte_en     => CONNECTED_TO_mem_mem_req_byte_en,     --       .mem_req_byte_en
			mem_mem_req_read_writen => CONNECTED_TO_mem_mem_req_read_writen, --       .mem_req_read_writen
			mem_mem_req_request     => CONNECTED_TO_mem_mem_req_request,     --       .mem_req_request
			mem_mem_req_tag         => CONNECTED_TO_mem_mem_req_tag,         --       .mem_req_tag
			mem_mem_req_wdata       => CONNECTED_TO_mem_mem_req_wdata,       --       .mem_req_wdata
			mem_mem_resp_dack_tag   => CONNECTED_TO_mem_mem_resp_dack_tag,   --       .mem_resp_dack_tag
			mem_mem_resp_data       => CONNECTED_TO_mem_mem_resp_data,       --       .mem_resp_data
			mem_mem_resp_rack_tag   => CONNECTED_TO_mem_mem_resp_rack_tag,   --       .mem_resp_rack_tag
			reset_reset_n           => CONNECTED_TO_reset_reset_n,           --  reset.reset_n
			dummy_export            => CONNECTED_TO_dummy_export             --  dummy.export
		);

