library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spi_peripheral_zpu is
generic (
    g_fixed_rate : boolean := false;
    g_init_rate  : integer := 500;
    g_crc        : boolean := true );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    bus_read    : in  std_logic;
    bus_write   : in  std_logic;
    bus_rack    : out std_logic;
    bus_dack    : out std_logic;
    
    bus_addr    : in  std_logic_vector(1 downto 0);
    bus_wdata   : in  std_logic_vector(7 downto 0);
    bus_rdata   : out std_logic_vector(7 downto 0);
        
    SD_DETECTn  : in  std_logic;
    SD_WRPROTn  : in  std_logic := '1';
    SPI_SSn     : out std_logic;
    SPI_CLK     : out std_logic;
    SPI_MOSI    : out std_logic;
    SPI_MISO    : in  std_logic );
end spi_peripheral_zpu;

architecture gideon of spi_peripheral_zpu is
    signal do_send     : std_logic;
    signal force_ss    : std_logic := '0';
    signal level_ss    : std_logic := '0';
    signal busy        : std_logic;
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
        busy        => busy,
                               
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
            bus_rack  <= '0';
            bus_dack  <= '0';
            bus_rdata <= (others => '0');
            
            case state is
            when idle =>
                if bus_write='1' then
                    state <= writing;
                elsif bus_read='1' then
                    state <= reading;
                end if;
            
            when writing =>
                if busy='0' then
                    bus_rack <= '1';
                    state <= idle;
                    
                    case bus_addr is
                    when "00" =>
                        do_send <= '1';
                        wdata   <= bus_wdata;
                        
                    when "01" =>
                        if not g_fixed_rate then
                            rate(7 downto 0) <= bus_wdata;
                            rate(8)          <= bus_wdata(7);
                        end if;
                                        
                    when "10" =>
                        force_ss <= bus_wdata(0);
                        level_ss <= bus_wdata(1);
                    
                    when "11" =>
                        clear_crc <= '1'; 
                    
                    when others =>
                        null;
                    end case;
                end if;
                
            when reading =>
                if busy='0' then
                    bus_rack <= '1';
                    case bus_addr is
                    when "00" =>
                        do_send <= '1';
                        wdata <= X"FF";
                        state <= receive;
                    
                    when "01" =>
                        bus_rdata <= rate(7 downto 0);
                        bus_dack <= '1';
                        state <= idle;
                    
                    when "10" =>
                        bus_rdata <= "0000" & not SD_WRPROTn & not SD_DETECTn & level_ss & force_ss;
                        bus_dack <= '1';
                        state <= idle;
                    
                    when "11" =>
                        bus_rdata <= crc_out;
                        bus_dack <= '1';
                        state <= idle;

                    when others =>
                        null;
                    end case;
                end if;
                            
            when receive =>
                if do_send = '0' and busy = '0' then
                    bus_rdata <= rdata;
                    bus_dack <= '1';
                    state <= idle;
                end if;

            when others =>
                null;
            end case;

            if reset='1' then
                rate     <= std_logic_vector(to_unsigned(g_init_rate, 9));
                force_ss <= '0';
                level_ss <= '1';
                wdata    <= (others => '0');
            end if;
        end if;        
    end process;
     
end gideon;
