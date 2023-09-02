library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity uart_dma is
generic (
    g_rx_tag    : std_logic_vector(7 downto 0) := X"14";
    g_tx_tag    : std_logic_vector(7 downto 0) := X"15";
	g_divisor	: natural := 35 );
port (
	clock		: in  std_logic;
	reset		: in  std_logic;
	
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
	irq         : out std_logic;
	
    mem_req     : out t_mem_req_32;
    mem_resp    : in  t_mem_resp_32;

	txd			: out std_logic;
	rxd			: in  std_logic := '1';
	rts         : out std_logic;
	cts         : in  std_logic := '1' );
end entity;

architecture gideon of uart_dma is
    signal divisor      : std_logic_vector(9 downto 0);
	signal dotx			: std_logic;
	signal done			: std_logic;
	signal rxchar		: std_logic_vector(7 downto 0);
	signal rx_ack		: std_logic := '0';
	signal rx_push		: std_logic := '0';
    signal rx_timeout   : std_logic := '0';
    signal rx_interrupt : std_logic := '0';
    signal tx_interrupt : std_logic := '0';
	signal overflow		: std_logic := '0';

    signal rxfifo_ready : std_logic;
    signal rxfifo_valid : std_logic;
    signal rxfifo_data  : std_logic_vector(7 downto 0);
    signal rxfifo_last  : std_logic;
    signal rxfifo_full  : std_logic := '0';
    signal rxfifo_afull : std_logic := '0';
	signal txchar		: std_logic_vector(7 downto 0);
    signal cts_c        : std_logic;
    signal cts_enable   : std_logic;

    signal rxd_i, txd_i   : std_logic;
    signal cts_i, rts_i   : std_logic;

    signal loopback       : std_logic;
    signal slip_enable    : std_logic;
    signal rx_irq_enable  : std_logic;
    signal tx_irq_enable  : std_logic;

    signal tx_data        : std_logic_vector(7 downto 0) := X"00";
    signal tx_valid       : std_logic;
    signal tx_last        : std_logic;
    signal tx_ready       : std_logic;

    signal rx_data        : std_logic_vector(7 downto 0) := X"00";
    signal rx_valid       : std_logic;
    signal rx_last        : std_logic;
    signal rx_ready       : std_logic;

    signal tx_mem_req     : t_mem_req_32;
    signal tx_mem_resp    : t_mem_resp_32;
    signal rx_mem_req     : t_mem_req_32;
    signal rx_mem_resp    : t_mem_resp_32;

    signal rx_addr_data     : std_logic_vector(27 downto 0);
    signal rx_addr_valid    : std_logic;
    signal rx_addr_ready    : std_logic;
    signal rx_len_data      : std_logic_vector(15 downto 0);
    signal rx_len_valid     : std_logic;
    signal rx_len_ready     : std_logic;

    signal tx_addr_data     : std_logic_vector(27 downto 0);
    signal tx_addr_user     : std_logic_vector(15 downto 0);
    signal tx_addr_valid    : std_logic;
    signal tx_addr_ready    : std_logic;

    constant c_uart_divisor_l   : unsigned(3 downto 0) := "0000";
    constant c_uart_divisor_h   : unsigned(3 downto 0) := "0001";
    constant c_uart_imask       : unsigned(3 downto 0) := "0010";
    constant c_uart_status      : unsigned(3 downto 0) := "0010";
    constant c_uart_flowctrl    : unsigned(3 downto 0) := "0011";

    constant c_uart_tx_addr_l   : unsigned(3 downto 0) := "0100";
    constant c_uart_tx_addr_ml  : unsigned(3 downto 0) := "0101";
    constant c_uart_tx_addr_mh  : unsigned(3 downto 0) := "0110";
    constant c_uart_tx_addr_h   : unsigned(3 downto 0) := "0111";
    constant c_uart_len_l       : unsigned(3 downto 0) := "1000";
    constant c_uart_len_h       : unsigned(3 downto 0) := "1001";
    constant c_uart_tx_push     : unsigned(3 downto 0) := "1010";
    constant c_uart_rx_get      : unsigned(3 downto 0) := "1011";

    constant c_uart_rx_addr_l   : unsigned(3 downto 0) := "1100";
    constant c_uart_rx_addr_ml  : unsigned(3 downto 0) := "1101";
    constant c_uart_rx_addr_mh  : unsigned(3 downto 0) := "1110";
    constant c_uart_rx_addr_h   : unsigned(3 downto 0) := "1111";

begin
    tx_dma_inst: entity work.tx_dma
    generic map (
        g_fifo_depth => 2047,
        g_mem_tag => g_tx_tag
    )
    port map (
        clock      => clock,
        reset      => reset,
        addr_data  => tx_addr_data,
        addr_user  => tx_addr_user,
        addr_valid => tx_addr_valid,
        addr_ready => tx_addr_ready,
        mem_req    => tx_mem_req,
        mem_resp   => tx_mem_resp,
        out_data   => tx_data,
        out_valid  => tx_valid,
        out_last   => tx_last,
        out_ready  => tx_ready
    );

    i_slip_encode: entity work.slip_encoder
    port map (
        clock       => clock,
        reset       => reset,
        slip_enable => slip_enable,
        in_data     => tx_data,
        in_last     => tx_last,
        in_valid    => tx_valid,
        in_ready    => tx_ready,
        out_data    => txchar,
        out_valid   => dotx,
        out_ready   => done
    );

    my_tx: entity work.tx 
    port map (
        divisor => divisor,

        clk     => clock,
        reset   => reset,
        tick    => '1',
    
        dotx    => dotx,
        txchar  => txchar,
        cts     => cts_c,
    
        txd     => txd_i,
        done    => done );

    my_rx: entity work.rx 
    port map (
        divisor => divisor,

        clk     => clock,
        reset   => reset,
        tick    => '1',
    
        rxd     => rxd_i,
        
        timeout => rx_timeout,
        rxchar  => rxchar,
        rx_ack  => rx_ack );

    rx_push <= rx_ack or (not slip_enable and rx_timeout);

    i_rxfifo: entity work.sync_fifo
    generic map(
        g_depth        => 511,
        g_data_width   => 9,
        g_threshold    => 500,
        g_fall_through => true
    )
    port map(
        clock          => clock,
        reset          => reset,
        wr_en          => rx_push,
        din(7 downto 0) => rxchar,
        din(8)          => rx_timeout,
        full           => rxfifo_full,
        almost_full    => rxfifo_afull,
        flush          => '0',

        dout(7 downto 0) => rxfifo_data,
        dout(8)          => rxfifo_last,
        rd_en          => rxfifo_ready,
        valid          => rxfifo_valid
    );

    slip_decoder_inst: entity work.slip_decoder
    port map (
        clock       => clock,
        reset       => reset,
        slip_enable => slip_enable,
        in_data     => rxfifo_data,
        in_last     => rxfifo_last,
        in_valid    => rxfifo_valid,
        in_ready    => rxfifo_ready,
        out_data    => rx_data,
        out_last    => rx_last,
        out_valid   => rx_valid,
        out_ready   => rx_ready
    );

    rx_dma_inst: entity work.rx_dma
    generic map (
        g_mem_tag  => g_rx_tag
    )
    port map (
        clock      => clock,
        reset      => reset,
        addr_data  => rx_addr_data,
        addr_valid => rx_addr_valid,
        addr_ready => rx_addr_ready,
        len_data   => rx_len_data,
        len_valid  => rx_len_valid,
        len_ready  => rx_len_ready,
        mem_req    => rx_mem_req,
        mem_resp   => rx_mem_resp,
        in_data    => rx_data,
        in_valid   => rx_valid,
        in_last    => rx_last,
        in_ready   => rx_ready
    );

    mem_bus_arbiter_pri_32_inst: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_registered => false,
        g_ports      => 2
    )
    port map (
        clock    => clock,
        reset    => reset,
        inhibit  => '0',
        reqs(1)  => tx_mem_req,
        reqs(0)  => rx_mem_req,
        resps(1) => tx_mem_resp,
        resps(0) => rx_mem_resp,
        req      => mem_req,
        resp     => mem_resp
    );

    -- External rts/cts signals are active low
    -- Internal signals are active high
    rxd_i <= txd_i when loopback = '1' else rxd;
    cts_i <= rts_i when loopback = '1' else not cts; 
    txd   <= txd_i or loopback;
    rts   <= not rts_i or loopback;
    rts_i <= not rxfifo_afull; -- active high (1 = go, 0 = block)

	process(clock)
	begin
		if rising_edge(clock) then
            io_resp    <= c_io_resp_init;
            cts_c      <= cts_i or not cts_enable;

            -- stream logic
            if tx_addr_ready = '1' then
                tx_addr_valid <= '0';
            end if;
            if rx_addr_ready = '1' then
                rx_addr_valid <= '0';
            end if;
            rx_len_ready <= '0';

			if io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_uart_divisor_l =>
                    divisor(7 downto 0) <= io_req.data;
                    
                when c_uart_divisor_h =>
                    divisor(9 downto 8) <= io_req.data(1 downto 0);

                when c_uart_imask =>
                    if io_req.data(7) = '1' then
                        rx_irq_enable <= rx_irq_enable or io_req.data(0);
                        tx_irq_enable <= tx_irq_enable or io_req.data(1);
                    else
                        rx_irq_enable <= rx_irq_enable and not io_req.data(0);
                        tx_irq_enable <= tx_irq_enable and not io_req.data(1);
                    end if;
                    if io_req.data(3) = '1' then
                        overflow <= '0';
                    end if;

                when c_uart_flowctrl =>
                    cts_enable  <= io_req.data(0);
                    loopback    <= io_req.data(1);
                    slip_enable <= io_req.data(2);

                when c_uart_tx_addr_l =>
                    tx_addr_data(7 downto 0) <= io_req.data;

                when c_uart_tx_addr_ml =>
                    tx_addr_data(15 downto 8) <= io_req.data;

                when c_uart_tx_addr_mh =>
                    tx_addr_data(23 downto 16) <= io_req.data;

                when c_uart_tx_addr_h =>
                    tx_addr_data(27 downto 24) <= io_req.data(3 downto 0);

                when c_uart_len_l =>
                    tx_addr_user(7 downto 0) <= io_req.data;

                when c_uart_len_h =>
                    tx_addr_user(15 downto 8) <= io_req.data;
                
                when c_uart_tx_push =>
                    tx_addr_valid <= '1';

                when c_uart_rx_get =>
                    rx_len_ready <= '1';

                when c_uart_rx_addr_l =>
                    rx_addr_data(7 downto 0) <= io_req.data;

                when c_uart_rx_addr_ml =>
                    rx_addr_data(15 downto 8) <= io_req.data;

                when c_uart_rx_addr_mh =>
                    rx_addr_data(23 downto 16) <= io_req.data;

                when c_uart_rx_addr_h =>
                    rx_addr_data(27 downto 24) <= io_req.data(3 downto 0);
                    rx_addr_valid <= '1';

				when others =>
					null;
					
				end case;
            elsif io_req.read='1' then
                io_resp.ack  <= '1';

                case io_req.address(3 downto 0) is
                when c_uart_divisor_l =>
                    io_resp.data <= divisor(7 downto 0);
                    
                when c_uart_divisor_h =>
                    io_resp.data(1 downto 0) <= divisor(9 downto 8);

                when c_uart_imask =>
                    io_resp.data(0) <= rx_len_valid;
                    io_resp.data(1) <= tx_addr_ready;
                    io_resp.data(2) <= cts_c;
                    io_resp.data(3) <= overflow;

                when c_uart_flowctrl =>
                    io_resp.data(0) <= cts_enable;
                    io_resp.data(1) <= loopback;
                    io_resp.data(2) <= slip_enable;

                when c_uart_len_l =>
                    io_resp.data <= rx_len_data(7 downto 0);

                when c_uart_len_h =>
                    io_resp.data <= rx_len_data(15 downto 8);

                when others =>
                    null;
                    
                end case;
			end if;

            if rxfifo_full='1' and rx_ack='1' then
                overflow <= '1';
            end if;

			if reset='1' then
                loopback <= '0';
                cts_enable <= '0';
                slip_enable <= '0';
				overflow <= '0';
                rx_irq_enable <= '0';
                tx_irq_enable <= '0';
				divisor  <= std_logic_vector(to_unsigned(g_divisor-1, divisor'length));
			end if;
		end if;
	end process;

    rx_interrupt <= rx_irq_enable and rx_len_valid;
    tx_interrupt <= tx_irq_enable and tx_addr_ready;
    irq <= rx_interrupt or tx_interrupt;
	
end gideon;
