library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rxtx_to_buf is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- bram interface
    ram_addr        : out std_logic_vector(10 downto 0);
    ram_wdata       : out std_logic_vector(7 downto 0);
    ram_rdata       : in  std_logic_vector(7 downto 0);
    ram_we          : out std_logic;
    ram_en          : out std_logic;
    
    transferred     : out unsigned(10 downto 0);
    
    -- Interface from RX
    user_rx_valid   : in  std_logic;
    user_rx_start   : in  std_logic;
    user_rx_data    : in  std_logic_vector(7 downto 0);
    user_rx_last    : in  std_logic;
    
    -- Interface to TX
    send_data       : in  std_logic;
    last_addr       : in  unsigned(10 downto 0);
    no_data         : in  std_logic := '0';
    user_tx_data    : out std_logic_vector(7 downto 0);
    user_tx_last    : out std_logic;
    user_tx_next    : in  std_logic );
end entity;

architecture gideon of rxtx_to_buf is
    signal ram_addr_i   : unsigned(10 downto 0);
    signal ram_en_r     : std_logic;
    type t_state is (idle, tx_1, tx_2, tx_3, rx);
    signal state        : t_state;
    signal trx_end       : std_logic;
begin    
    ram_en <= ram_en_r or user_rx_valid or user_tx_next;
    ram_we <= user_rx_valid;
    ram_wdata <= user_rx_data;
    user_tx_data <= ram_rdata;
    ram_addr <= std_logic_vector(ram_addr_i);

    process(clock)
    begin
        if rising_edge(clock) then
            ram_en_r <= '0';
            trx_end <= '0';
            if trx_end = '1' then
                transferred <= ram_addr_i;
            end if;

            case state is
            when idle =>
                ram_addr_i <= (others => '0');
                if send_data='1' and no_data='0' then
                    ram_en_r <= '1';
                    state <= tx_1;
                elsif user_rx_start='1' then
                    if user_rx_valid='1' then
                        ram_addr_i <= ram_addr_i + 1;
                    end if;
                    state <= rx; 
                end if;
            when tx_1 =>
                ram_addr_i <= ram_addr_i + 1;
                state <= tx_2;
            when tx_2 =>
                if user_tx_next='1' then
                    ram_addr_i <= ram_addr_i + 1;
                    if ram_addr_i = last_addr then
                        user_tx_last <= '1';
                        state <= tx_3;
                    end if;
                end if;
            when tx_3 =>
                if user_tx_next='1' then
                    trx_end <= '1';
                    state <= idle;
                    user_tx_last <= '0';
                end if;
            when rx =>
                if user_rx_valid='1' then
                    ram_addr_i <= ram_addr_i + 1;
                end if;
                if user_rx_last='1' then
                    trx_end <= '1';
                    state <= idle;
                end if;
            when others =>
                null;
            end case;
            if reset='1' then
                state <= idle;
                user_tx_last <= '0';
            end if;
        end if;
    end process;
end architecture;
