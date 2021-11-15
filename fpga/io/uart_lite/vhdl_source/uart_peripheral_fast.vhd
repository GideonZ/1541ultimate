library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity uart_peripheral_fast is
generic (
	g_divisor	: natural := 35 );
port (
	clock		: in  std_logic;
	reset		: in  std_logic;
	
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
	irq         : out std_logic;
	
	txd			: out std_logic;
	rxd			: in  std_logic := '1';
	rts         : out std_logic;
	cts         : in  std_logic := '1' );
end uart_peripheral_fast;

architecture gideon of uart_peripheral_fast is
    signal divisor      : std_logic_vector(9 downto 0);
	signal dotx			: std_logic;
	signal done			: std_logic;
	signal rxchar		: std_logic_vector(7 downto 0);
	signal rx_ack		: std_logic := '0';
    signal rx_timeout   : std_logic := '0';
    signal rx_timeout_reg : std_logic := '0';
	signal rxfifo_get	: std_logic := '0';
	signal rxfifo_dout	: std_logic_vector(7 downto 0) := X"00";
    signal rxfifo_afull : std_logic := '0';
	signal rxfifo_full	: std_logic := '0';
	signal rxfifo_dav	: std_logic := '0';
    signal rx_interrupt : std_logic := '0';
    signal tx_interrupt : std_logic := '0';
	signal overflow		: std_logic := '0';
	signal flags		: std_logic_vector(7 downto 0);
	signal imask		: std_logic_vector(1 downto 0);
    signal rdata_mux    : std_logic_vector(7 downto 0);

    signal txfifo_get   : std_logic;
    signal txfifo_put   : std_logic;
    signal txfifo_dout  : std_logic_vector(7 downto 0);
    signal txfifo_aempty: std_logic := '1';
    signal txfifo_full  : std_logic := '0';
    signal txfifo_dav   : std_logic;
    signal dotx_d       : std_logic;
	signal txchar		: std_logic_vector(7 downto 0);
    signal cts_c        : std_logic;
    signal cts_enable   : std_logic;

    signal rxd_i, txd_i     : std_logic;
    signal cts_i, rts_i     : std_logic;
    signal loopback         : std_logic;
    
    signal tx_count     : integer range 0 to 255;
    signal rx_count     : integer range 0 to 255;
    
    constant c_uart_data        : unsigned(2 downto 0) := "000";
    constant c_uart_flags       : unsigned(2 downto 0) := "001";
    constant c_uart_imask       : unsigned(2 downto 0) := "010";
    constant c_uart_flowctrl    : unsigned(2 downto 0) := "011";
    constant c_uart_divisor_l   : unsigned(2 downto 0) := "100";
    constant c_uart_divisor_h   : unsigned(2 downto 0) := "101";
    constant c_uart_rx_count    : unsigned(2 downto 0) := "110";
    constant c_uart_tx_count    : unsigned(2 downto 0) := "111";
begin
    i_tx_fifo: entity work.sync_fifo
    generic map (
        g_depth        => 255,
        g_data_width   => 8,
        g_threshold    => 240,
        g_threshold_low=> 15,
        g_fall_through => true
    )
    port map(
        clock          => clock,
        reset          => reset,
        rd_en          => txfifo_get,
        wr_en          => txfifo_put,
        din            => io_req.data,
        dout           => txfifo_dout,
        flush          => '0',
        count          => tx_count,
        full           => txfifo_full,
        almost_empty   => txfifo_aempty,
        valid          => txfifo_dav
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

    -- External rts/cts signals are active low
    -- Internal signals are active high
    rxd_i <= txd_i when loopback = '1' else rxd;
    cts_i <= rts_i when loopback = '1' else not cts; 

    txd   <= txd_i or loopback;
    rts   <= not rts_i or loopback;

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

    i_rxfifo: entity work.sync_fifo
    generic map(
        g_depth        => 255,
        g_data_width   => 8,
        g_threshold    => 240,
        g_fall_through => true
    )
    port map(
        clock          => clock,
        reset          => reset,
        wr_en          => rx_ack,
        din            => rxchar,

        rd_en          => rxfifo_get,
        dout           => rxfifo_dout,
        flush          => '0',
        count          => rx_count,
        full           => rxfifo_full,
        almost_full    => rxfifo_afull,
        valid          => rxfifo_dav
    );
    
    txfifo_put <= '1' when io_req.write = '1' and io_req.address(1 downto 0) = c_uart_data else '0'; 

	process(clock)
	begin
		if rising_edge(clock) then
			rxfifo_get <= '0';
			dotx_d     <= dotx;
			txfifo_get <= dotx_d;
            io_resp    <= c_io_resp_init;
			cts_c      <= cts_i or not cts_enable;
			
            dotx   <= txfifo_dav and done and not dotx;
            txchar <= txfifo_dout;
            
			if io_req.write='1' then
                io_resp.ack <= '1';
				case io_req.address(2 downto 0) is
				when c_uart_data => -- dout
                    -- outside of process

				when c_uart_flags => -- clear flags
					overflow <= overflow and not io_req.data(0);
                    rx_timeout_reg <= rx_timeout_reg and not io_req.data(6);
                    
				when c_uart_imask => -- interrupt control
					imask <= io_req.data(1 downto 0);

                when c_uart_divisor_l =>
                    divisor(7 downto 0) <= io_req.data;
                    
                when c_uart_divisor_h =>
                    divisor(9 downto 8) <= io_req.data(1 downto 0);

                when c_uart_flowctrl =>
                    cts_enable <= io_req.data(0);
                    loopback   <= io_req.data(1);

				when others =>
					null;
					
				end case;
            elsif io_req.read='1' then
                io_resp.ack  <= '1';
                io_resp.data <= rdata_mux;

                case io_req.address(2 downto 0) is
                when c_uart_data => -- dout
                    rxfifo_get <= '1';

                when others =>
                    null;
                    
                end case;
			end if;

            if rx_timeout = '1' then
                rx_timeout_reg <= '1';
            end if;

            if rxfifo_full='1' and rx_ack='1' then
                overflow <= '1';
            end if;

			if reset='1' then
                loopback <= '0';
                cts_enable <= '0';
				overflow <= '0';
				imask    <= (others => '0');
				divisor  <= std_logic_vector(to_unsigned(g_divisor-1, divisor'length));
			end if;
		end if;
	end process;

    rx_interrupt <= (rxfifo_afull or rx_timeout_reg) and imask(0);
    tx_interrupt <= txfifo_aempty and imask(1);
    irq <= rx_interrupt or tx_interrupt;

	flags(0) <= overflow;
	flags(1) <= cts_c;
	flags(2) <= tx_interrupt;
	flags(3) <= txfifo_full;
	flags(4) <= rx_interrupt;
	flags(5) <= rxfifo_afull;
	flags(6) <= rx_timeout_reg;
	flags(7) <= rxfifo_dav;
	
    rts_i <= not rxfifo_afull; -- active high (1 = go, 0 = block)
    
	with io_req.address(2 downto 0) select rdata_mux <=
		rxfifo_dout                                when c_uart_data,
		flags                                      when c_uart_flags,
		"000000" & imask                           when c_uart_imask,
		divisor(7 downto 0)                        when c_uart_divisor_l,
		"000000" & divisor(9 downto 8)             when c_uart_divisor_h,
        "000000" & loopback & cts_enable           when c_uart_flowctrl,
        std_logic_vector(to_unsigned(tx_count, 8)) when c_uart_tx_count,
        std_logic_vector(to_unsigned(rx_count, 8)) when c_uart_rx_count,
		X"00"                                      when others;

end gideon;
