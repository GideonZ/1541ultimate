library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity tb_spi is
end tb_spi;

architecture tb of tb_spi is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal do_send     : std_logic;
    signal force_ss    : std_logic := '0';
    signal level_ss    : std_logic := '0';
    signal busy        : std_logic;
    signal rate        : std_logic_vector(7 downto 0) := X"01";
    signal cpol        : std_logic := '0';
    signal cpha        : std_logic := '0';
    signal wdata       : std_logic_vector(7 downto 0);
    signal rdata       : std_logic_vector(7 downto 0);
    signal SPI_SSn     : std_logic;
    signal SPI_CLK     : std_logic;
    signal SPI_MOSI    : std_logic;
    --signal SPI_MISO    : std_logic;
    signal stop_clock  : boolean := false;
    signal clear_crc   : std_logic := '0';
begin
    clock <= not clock after 10 ns when not stop_clock;
    reset <= '1', '0' after 100 ns;
    
    mut: entity work.spi
    port map (
        clock       => clock,
        reset       => reset,
                               
        do_send     => do_send,
        clear_crc   => clear_crc,
        force_ss    => force_ss,
        level_ss    => level_ss,
        busy        => busy,
                               
        rate(8)     => rate(7),
        rate(7 downto 0) => rate,
        cpol        => cpol,
        cpha        => cpha,
                               
        wdata       => wdata,
        rdata       => rdata,
                               
        SPI_SSn     => SPI_SSn,
        SPI_CLK     => SPI_CLK,
        SPI_MOSI    => SPI_MOSI,
        SPI_MISO    => SPI_MOSI );

    test: process
        procedure send_byte(byte: std_logic_vector) is
        begin
            wdata <= byte;
            wait until clock='1';
            if busy='1' then
                wait until busy='0';
            end if;
            do_send <= '1';
            wait until clock='1';
            do_send <= '0';
        end procedure;
    begin
        cpol <= '0';
        cpha <= '0';
        do_send <= '0';
        wdata   <= X"00";
        wait until reset='0';
        wait for 200 ns;
        wait until clock='1';
        clear_crc <= '1';
        wait until clock='1';
        clear_crc <= '0';
        
        send_byte(X"40");
        send_byte(X"00");
        send_byte(X"00");
        send_byte(X"00");
        send_byte(X"00");
        send_byte(X"95");

        wait until busy='0';

        wait for 200 ns;
        stop_clock <= true;
        wait;
    end process;
end tb;
