library ieee;
use ieee.std_logic_1164.all;

entity uart_peripheral_zpu is
generic (
    tx_fifo     : boolean := true;
	divisor		: natural := 417 );
port (
	clock		: in  std_logic;
	reset		: in  std_logic;
	
    bus_read    : in  std_logic;
    bus_write   : in  std_logic;
    bus_rack    : out std_logic;
    bus_dack    : out std_logic;
    bus_addr    : in  std_logic_vector(3 downto 2);
    bus_wdata   : in  std_logic_vector(7 downto 0);
    bus_rdata   : out std_logic_vector(7 downto 0);
	
	uart_irq	: out std_logic;

	txd			: out std_logic;
	rxd			: in  std_logic );
end uart_peripheral_zpu;

architecture gideon of uart_peripheral_zpu is
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
    
begin
    my_tx: entity work.tx 
    generic map (divisor)
    port map (
        clk     => clock,
        reset   => reset,
        
        dotx    => dotx,
        txchar  => txchar,
    
        txd     => txd,
        done    => done );

    my_rx: entity work.rx 
    generic map (divisor)
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

    gentx: if tx_fifo generate
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
    	    DataIn      => bus_wdata,
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
			bus_rack   <= '0';
			bus_dack   <= '0';
            bus_rdata  <= (others => '0');
			
			if rxfifo_full='1' and rx_ack='1' then
				overflow <= '1';
			end if;
			
            txfifo_put <= '0';

            if tx_fifo then
                dotx   <= txfifo_dav and done and not dotx;
                txchar <= txfifo_dout;
            else            
                dotx <= '0';
            end if;
            
			if bus_write='1' then
                bus_rack <= '1';
				case bus_addr is
				when "00" => -- dout
                    if not tx_fifo then
                        txchar  <= bus_wdata;
                        dotx <= '1';
                    else -- there is a fifo 
                        txfifo_put <= '1';
                    end if;

				when "01" => -- din
					rxfifo_get <= '1';
			
				when "10" => -- clear flags
					overflow <= overflow and not bus_wdata(0);

				when "11" => -- interrupt control
					imask <= bus_wdata(7 downto 6);

				when others =>
					null;
					
				end case;
            elsif bus_read='1' then
                bus_rack  <= '1';
                bus_rdata <= rdata_mux;
                bus_dack  <= '1';
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
	
	with bus_addr select rdata_mux <=
		rxfifo_dout      when "00",
		flags            when "10",
		imask & "000000" when "11",
		X"00"            when others;

	uart_irq <= '1' when (flags(7 downto 6) and imask) /= "00" else '0';

end gideon;
