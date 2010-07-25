library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity spi is
generic (
    g_crc        : boolean := true );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    do_send     : in  std_logic;
    clear_crc   : in  std_logic;
    force_ss    : in  std_logic;
    level_ss    : in  std_logic;
    busy        : out std_logic;
    
    rate        : in  std_logic_vector(8 downto 0);
    cpol        : in  std_logic;
    cpha        : in  std_logic;
    
    wdata       : in  std_logic_vector(7 downto 0);
    rdata       : out std_logic_vector(7 downto 0);
    crc_out     : out std_logic_vector(7 downto 0);
    
    SPI_SSn     : out std_logic;
    SPI_CLK     : out std_logic;
    SPI_MOSI    : out std_logic;
    SPI_MISO    : in  std_logic );
end spi;

architecture gideon of spi is
    signal bit_cnt  : std_logic_vector(3 downto 0);
    signal delay    : std_logic_vector(8 downto 0);
    type t_state is (idle, transceive, done, gap);
    signal state    : t_state;
    signal shift    : std_logic_vector(7 downto 0) := X"FF";
    signal crc      : std_logic_vector(6 downto 0) := (others => '0');
begin
    process(clock)
        procedure update_crc(din : std_logic) is
        begin
            crc(6 downto 1) <= crc(5 downto 0);
            crc(0) <= din xor crc(6);
            crc(3) <= crc(2) xor din xor crc(6);
        end procedure;

        variable s : std_logic;
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                SPI_SSn <= '1';
                SPI_CLK <= cpol;
                delay   <= rate;
                bit_cnt <= "0000";
                
                if do_send='1' then
                    busy     <= '1';
                    state    <= transceive;
                    SPI_SSn  <= '0';
                    if cpha='0' then -- output first bit immediately
                        update_crc(wdata(7));
                        SPI_MOSI <= wdata(7);
                        shift    <= wdata(6 downto 0) & '0';
                    else -- output first bit upon shift edge
                        shift    <= wdata;
                    end if;
                end if;
            
            when transceive =>
                if delay = 0 then
                    delay <= rate;
                    bit_cnt <= bit_cnt + 1;
                    SPI_CLK <= not bit_cnt(0) xor cpol;
                    s := cpha xor bit_cnt(0);

                    if s = '0' then
                        shift(0) <= SPI_MISO;
                    end if;

                    if bit_cnt = "1111" then
                        state <= done;
                    else
                        if s = '1' then
                            update_crc(shift(7));
                            SPI_MOSI <= shift(7);
                            shift <= shift(6 downto 0) & '0';
                        end if;
                    end if;
                else
                    delay <= delay - 1;
                end if;

            when done =>
                if delay = 0 then
                    delay   <= rate;
                    rdata   <= shift;
                    SPI_SSn <= '1';
                    state   <= gap;
                else
                    delay <= delay - 1;
                end if;
                
            when gap =>
                if delay = 0 then
                    state <= idle;
                    busy    <= '0';
                else
                    delay <= delay - 1;
                end if;
            
            when others =>
                null;
            end case;

            if clear_crc='1' then
                crc <= (others => '0');
            end if;
            if reset='1' then
                state <= idle;
                rdata <= X"00";
                busy  <= '0';
                SPI_MOSI <= '1';                
                crc   <= (others => '0');
            end if;
            if force_ss='1' then
                SPI_SSn <= level_ss;
            end if;
        end if;        
    end process;
    
    crc_out <= crc & '1' when g_crc else X"00";
end gideon;
