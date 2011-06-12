library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity uart_peripheral_io is
generic (
    g_tx_fifo   : boolean := true;
	g_divisor	: natural := 417 );
port (
	clock		: in  std_logic;
	reset		: in  std_logic;
	
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
	
	uart_irq	: out std_logic;

	txd			: out std_logic;
	rxd			: in  std_logic := '1';
	rts         : out std_logic;
	cts         : in  std_logic := '1' );
end uart_peripheral_io;

architecture gideon of uart_peripheral_io is
	signal dotx			: std_logic;
	signal done			: std_logic;
	signal rxchar		: std_logic_vector(7 downto 0);
	signal rx_ack		: std_logic;
	signal rxfifo_get	: std_logic;
	signal rxfifo_dout	: std_logic_vector(7 downto 0);
	signal rxfifo_full	: std_logic;
	signal rxfifo_dav	: std_logic;
	signal overflow		: std_logic;
	signal flags		: std_logic_vector(7 downto 0);
	signal imask		: std_logic_vector(7 downto 6);
    signal rdata_mux    : std_logic_vector(7 downto 0);

    signal txfifo_get   : std_logic;
    signal txfifo_put   : std_logic;
    signal txfifo_dout  : std_logic_vector(7 downto 0);
    signal txfifo_full  : std_logic := '1';
    signal txfifo_dav   : std_logic;
    signal dotx_d       : std_logic;
	signal txchar		: std_logic_vector(7 downto 0);
    
    constant c_uart_data    : unsigned(1 downto 0) := "00";
    constant c_uart_get     : unsigned(1 downto 0) := "01";
    constant c_uart_flags   : unsigned(1 downto 0) := "10";
    constant c_uart_imask   : unsigned(1 downto 0) := "11";
begin
    my_tx: entity work.tx 
    generic map (g_divisor)
    port map (
        clk     => clock,
        reset   => reset,
        
        dotx    => dotx,
        txchar  => txchar,
        cts     => cts,
    
        txd     => txd,
        done    => done );

    my_rx: entity work.rx 
    generic map (g_divisor)
    port map (
        clk     => clock,
        reset   => reset,
    
        rxd     => rxd,
        
        rxchar  => rxchar,
        rx_ack  => rx_ack );

	my_rxfifo: entity work.srl_fifo
	generic map (
		Width     => 8,
        Threshold => 12 )
	port map (
	    clock       => clock,
	    reset       => reset,
	    GetElement  => rxfifo_get,
	    PutElement  => rx_ack,
	    FlushFifo   => '0',
	    DataIn      => rxchar,
	    DataOut     => rxfifo_dout,
	    SpaceInFifo => open,
	    AlmostFull  => rxfifo_full,
	    DataInFifo  => rxfifo_dav );

    gentx: if g_tx_fifo generate
    	my_txfifo: entity work.srl_fifo
    	generic map (
    		Width     => 8,
            Threshold => 12 )
    	port map (
    	    clock       => clock,
    	    reset       => reset,
    	    GetElement  => txfifo_get,
    	    PutElement  => txfifo_put,
    	    FlushFifo   => '0',
    	    DataIn      => io_req.data,
    	    DataOut     => txfifo_dout,
    	    SpaceInFifo => open,
    	    AlmostFull  => txfifo_full,
    	    DataInFifo  => txfifo_dav );
    end generate;

	process(clock)
	begin
		if rising_edge(clock) then
			rxfifo_get <= '0';
			dotx_d     <= dotx;
			txfifo_get <= dotx_d;
            io_resp    <= c_io_resp_init;
			
			if rxfifo_full='1' and rx_ack='1' then
				overflow <= '1';
			end if;
			
            txfifo_put <= '0';

            if g_tx_fifo then
                dotx   <= txfifo_dav and done and not dotx;
                txchar <= txfifo_dout;
            else            
                dotx <= '0'; -- default, overridden with write
            end if;
            
			if io_req.write='1' then
                io_resp.ack <= '1';
				case io_req.address(1 downto 0) is
				when c_uart_data => -- dout
                    if not g_tx_fifo then
                        txchar  <= io_req.data;
                        dotx <= '1';
                    else -- there is a fifo 
                        txfifo_put <= '1';
                    end if;

				when c_uart_get => -- din
					rxfifo_get <= '1';
			
				when c_uart_flags => -- clear flags
					overflow <= overflow and not io_req.data(0);

				when c_uart_imask => -- interrupt control
					imask <= io_req.data(7 downto 6);

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
			end if;
		end if;
	end process;

	flags(0) <= overflow;
	flags(1) <= '0';
	flags(2) <= '0';
	flags(3) <= '0';
	flags(4) <= txfifo_full;
	flags(5) <= rxfifo_full;
	flags(6) <= done;
	flags(7) <= rxfifo_dav;
	
    rts <= not rxfifo_full;
    
	with io_req.address(1 downto 0) select rdata_mux <=
		rxfifo_dout      when c_uart_data,
		flags            when c_uart_flags,
		imask & "000000" when c_uart_imask,
		X"00"            when others;

	uart_irq <= '1' when (flags(7 downto 6) and imask) /= "00" else '0';

end gideon;
