library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity uart_peripheral_io is
generic (
    g_impl_irq  : boolean := false;
    g_impl_rx   : boolean := true;
    g_tx_fifo   : boolean := true;
    g_big_fifo  : boolean := false;
	g_divisor	: natural := 35 );
port (
	clock		: in  std_logic;
	reset		: in  std_logic;
	
    tick        : in  std_logic;
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
	irq         : out std_logic;
	
	txd			: out std_logic;
	rxd			: in  std_logic := '1';
	rts         : out std_logic;
	cts         : in  std_logic := '1' );
end uart_peripheral_io;

architecture gideon of uart_peripheral_io is
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
	signal imask		: std_logic_vector(1 downto 0) := "00";
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
    constant c_divisor  : std_logic_vector(10 downto 0) := std_logic_vector(to_unsigned(g_divisor - 1, 11));
begin
    my_tx: entity work.tx 
    port map (
        divisor => c_divisor,

        clk     => clock,
        reset   => reset,
        tick    => tick,
    
        dotx    => dotx,
        txchar  => txchar,
        cts     => cts,
    
        txd     => txd,
        done    => done );

    r_rx: if g_impl_rx generate
        my_rx: entity work.rx 
        port map (
            divisor => c_divisor,

            clk     => clock,
            reset   => reset,
            tick    => tick,
        
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
    end generate;

    gentx: if g_tx_fifo generate
        r_big: if g_big_fifo generate
            i_tx_fifo: entity work.sync_fifo
            generic map (
                g_depth        => 1023,
                g_data_width   => 8,
                g_threshold    => 1000,
                g_storage      => "auto",
                g_fall_through => true
            )
            port map (
                clock        => clock,
                reset        => reset,
                rd_en        => txfifo_get,
                wr_en        => txfifo_put,
                din          => io_req.data,
                dout         => txfifo_dout,
                flush        => '0',
                full         => open,
                almost_full  => txfifo_full,
                valid        => txfifo_dav,
                count        => open
            );
        end generate;
        r_small: if not g_big_fifo generate
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
                    if g_impl_irq then
                        imask <= io_req.data(1 downto 0);
                    end if;

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

    irq <= (flags(6) and imask(1)) or (flags(7) and imask(0));

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
		"000000" & imask when c_uart_imask,
		X"00"            when others;

end gideon;
