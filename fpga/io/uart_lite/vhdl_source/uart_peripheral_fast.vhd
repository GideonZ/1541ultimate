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
	signal rxfifo_get	: std_logic := '0';
	signal rxfifo_dout	: std_logic_vector(7 downto 0) := X"00";
	signal rxfifo_full	: std_logic := '0';
	signal rxfifo_dav	: std_logic := '0';
	signal overflow		: std_logic := '0';
	signal flags		: std_logic_vector(7 downto 0);
	signal imask		: std_logic_vector(1 downto 0);
    signal rdata_mux    : std_logic_vector(7 downto 0);

    signal txfifo_get   : std_logic;
    signal txfifo_put   : std_logic;
    signal txfifo_dout  : std_logic_vector(7 downto 0);
    signal txfifo_full  : std_logic := '1';
    signal txfifo_afull : std_logic := '1';
    signal txfifo_empty : std_logic := '0';
    signal txfifo_dav   : std_logic;
    signal dotx_d       : std_logic;
	signal txchar		: std_logic_vector(7 downto 0);
    signal cts_c        : std_logic;
    signal cts_enable   : std_logic;
    
    constant c_uart_data        : unsigned(2 downto 0) := "000";
    constant c_uart_get         : unsigned(2 downto 0) := "001";
    constant c_uart_flags       : unsigned(2 downto 0) := "010";
    constant c_uart_imask       : unsigned(2 downto 0) := "011";
    constant c_uart_divisor_l   : unsigned(2 downto 0) := "100";
    constant c_uart_divisor_h   : unsigned(2 downto 0) := "101";
    constant c_uart_flowctrl    : unsigned(2 downto 0) := "110";
begin
    i_tx_fifo: entity work.sync_fifo
    generic map (
        g_depth        => 1024,
        g_data_width   => 8,
        g_threshold    => 512,
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
        empty          => txfifo_empty,
        almost_full    => txfifo_afull,
        full           => txfifo_full,
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
    
        txd     => txd,
        done    => done );

    my_rx: entity work.rx 
    port map (
        divisor => divisor,

        clk     => clock,
        reset   => reset,
        tick    => '1',
    
        rxd     => rxd,
        
        rxchar  => rxchar,
        rx_ack  => rx_ack );

    i_rxfifo: entity work.sync_fifo
    generic map(
        g_depth        => 1024,
        g_data_width   => 8,
        g_threshold    => 1000,
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
        almost_full    => rxfifo_full,
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
			cts_c      <= cts or not cts_enable;
			
			if rxfifo_full='1' and rx_ack='1' then
				overflow <= '1';
			end if;
			
            dotx   <= txfifo_dav and done and not dotx;
            txchar <= txfifo_dout;
            
			if io_req.write='1' then
                io_resp.ack <= '1';
				case io_req.address(2 downto 0) is
				when c_uart_data => -- dout
                    -- outside of process

				when c_uart_get => -- din
					rxfifo_get <= '1';
			
				when c_uart_flags => -- clear flags
					overflow <= overflow and not io_req.data(0);

				when c_uart_imask => -- interrupt control
					imask <= io_req.data(1 downto 0);

                when c_uart_divisor_l =>
                    divisor(7 downto 0) <= io_req.data;
                    
                when c_uart_divisor_h =>
                    divisor(9 downto 8) <= io_req.data(1 downto 0);

                when c_uart_flowctrl =>
                    cts_enable <= io_req.data(0);

				when others =>
					null;
					
				end case;
            elsif io_req.read='1' then
                io_resp.ack  <= '1';
                io_resp.data <= rdata_mux;
			end if;

			if reset='1' then
				overflow <= '0';
				imask    <= (others => '0');
				divisor  <= std_logic_vector(to_unsigned(g_divisor-1, divisor'length));
			end if;
		end if;
	end process;

    irq <= (flags(3) and imask(1)) or (flags(7) and imask(0));

	flags(0) <= overflow;
	flags(1) <= cts_c;
	flags(2) <= txfifo_empty;
	flags(3) <= not txfifo_afull;
	flags(4) <= txfifo_full;
	flags(5) <= rxfifo_full;
	flags(6) <= done;
	flags(7) <= rxfifo_dav;
	
    rts <= not rxfifo_full;
    
	with io_req.address(2 downto 0) select rdata_mux <=
		rxfifo_dout      when c_uart_data,
		flags            when c_uart_flags,
		"000000" & imask when c_uart_imask,
		divisor(7 downto 0) when c_uart_divisor_l,
		"000000" & divisor(9 downto 8) when c_uart_divisor_h,
        "0000000" & cts_enable when c_uart_flowctrl,
		X"00"            when others;

end gideon;
