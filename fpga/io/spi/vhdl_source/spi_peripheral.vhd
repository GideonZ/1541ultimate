library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spi_peripheral is
generic (
    g_fixed_rate : boolean := false;
    g_init_rate  : integer := 500;
    g_crc        : boolean := true );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    bus_select  : in  std_logic;
    bus_write   : in  std_logic;
    bus_addr    : in  std_logic_vector(1 downto 0);
    bus_wdata   : in  std_logic_vector(7 downto 0);
    bus_rdata   : out std_logic_vector(7 downto 0);
        
    SPI_SSn     : out std_logic;
    SPI_CLK     : out std_logic;
    SPI_MOSI    : out std_logic;
    SPI_MISO    : in  std_logic );
end spi_peripheral;

architecture gideon of spi_peripheral is
    signal do_send     : std_logic;
    signal force_ss    : std_logic := '0';
    signal level_ss    : std_logic := '0';
    signal busy        : std_logic;
    signal rate        : std_logic_vector(8 downto 0) := std_logic_vector(to_unsigned(g_init_rate, 9));
    signal rdata       : std_logic_vector(7 downto 0);
    signal wdata       : std_logic_vector(7 downto 0);
    signal clear_crc   : std_logic;
    signal crc_out     : std_logic_vector(7 downto 0);
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
            if bus_select='1' and bus_write='1' then
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

            if reset='1' then
                rate     <= std_logic_vector(to_unsigned(g_init_rate, 9));
                force_ss <= '0';
                level_ss <= '1';
                wdata    <= (others => '0');
            end if;
        end if;        
    end process;
    
    with bus_addr select bus_rdata <=
        rdata                                 when "00",
        rate(7 downto 0)                      when "01",
        busy & "00000" & level_ss & force_ss  when "10",
        crc_out                               when "11",
        X"FF"                                 when others;        
    
end gideon;
