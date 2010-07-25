library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity spi_peripheral_io is
generic (
    g_fixed_rate : boolean := false;
    g_init_rate  : integer := 500;
    g_crc        : boolean := true );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

	busy		: out std_logic;
	        
    SD_DETECTn  : in  std_logic := '1';
    SD_WRPROTn  : in  std_logic := '1';
    SPI_SSn     : out std_logic;
    SPI_CLK     : out std_logic;
    SPI_MOSI    : out std_logic;
    SPI_MISO    : in  std_logic );
end spi_peripheral_io;

architecture gideon of spi_peripheral_io is
    signal do_send     : std_logic;
    signal force_ss    : std_logic := '0';
    signal level_ss    : std_logic := '0';
    signal busy_i      : std_logic;
    signal rate        : std_logic_vector(8 downto 0) := std_logic_vector(to_unsigned(g_init_rate, 9));
    signal rdata       : std_logic_vector(7 downto 0);
    signal wdata       : std_logic_vector(7 downto 0);
    signal clear_crc   : std_logic;
    signal crc_out     : std_logic_vector(7 downto 0);
    
    type t_state is (idle, writing, reading, receive);
    signal state       : t_state;
    
begin
    spi1: entity work.spi
    generic map (
        g_crc       => g_crc )
    port map (
        clock       => clock,
        reset       => reset,
                               
        do_send     => do_send,
        clear_crc   => clear_crc,
        force_ss    => force_ss,
        level_ss    => level_ss,
        busy        => busy_i,
                               
        rate        => rate,
        cpol        => '0',
        cpha        => '0',
                               
        wdata       => wdata,
        rdata       => rdata,
        crc_out     => crc_out,
                               
        SPI_SSn     => SPI_SSn,
        SPI_CLK     => SPI_CLK,
        SPI_MOSI    => SPI_MOSI,
        SPI_MISO    => SPI_MISO );

    process(clock)
    begin
        if rising_edge(clock) then
            do_send   <= '0';
            clear_crc <= '0';
            io_resp   <= c_io_resp_init;
            
            case state is
            when idle =>
                if io_req.write='1' then
                    state <= writing;
                elsif io_req.read='1' then
                    state <= reading;
                end if;
            
            when writing =>
                if busy_i='0' then
                    io_resp.ack <= '1';
                    state <= idle;
                    
                    case io_req.address(3 downto 2) is
                    when "00" =>
                        do_send <= '1';
                        wdata   <= io_req.data;
                        
                    when "01" =>
                        if not g_fixed_rate then
                            rate(7 downto 0) <= io_req.data;
                            rate(8)          <= io_req.data(7);
                        end if;
                                        
                    when "10" =>
                        force_ss <= io_req.data(0);
                        level_ss <= io_req.data(1);
                    
                    when "11" =>
                        clear_crc <= '1'; 
                    
                    when others =>
                        null;
                    end case;
                end if;
                
            when reading =>
                if busy_i='0' then
                    case io_req.address(3 downto 2) is
                    when "00" =>
                        do_send <= '1';
                        wdata <= X"FF";
                        state <= receive;
                    
                    when "01" =>
                        io_resp.data <= rate(7 downto 0);
                        io_resp.ack <= '1';
                        state <= idle;
                    
                    when "10" =>
                        io_resp.data <= "0000" & not SD_WRPROTn & not SD_DETECTn & level_ss & force_ss;
                        io_resp.ack <= '1';
                        state <= idle;
                    
                    when "11" =>
                        io_resp.data <= crc_out;
                        io_resp.ack <= '1';
                        state <= idle;

                    when others =>
                        null;
                    end case;
                end if;
                            
            when receive =>
                if do_send = '0' and busy_i = '0' then
                    io_resp.data <= rdata;
                    io_resp.ack <= '1';
                    state <= idle;
                end if;

            when others =>
                null;
            end case;

            if reset='1' then
                if not g_fixed_rate then
                    rate     <= std_logic_vector(to_unsigned(g_init_rate, 9));
                end if;
                force_ss <= '0';
                level_ss <= '1';
                wdata    <= X"FF";
            end if;
        end if;        
    end process;

	busy <= busy_i;     
end gideon;
