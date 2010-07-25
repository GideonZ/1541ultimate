library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_ulpi_bus is

end entity;

architecture tb of tb_ulpi_bus is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal ULPI_DATA   : std_logic_vector(7 downto 0);
    signal ULPI_DIR    : std_logic;
    signal ULPI_NXT    : std_logic;
    signal ULPI_STP    : std_logic;
    signal tx_data     : std_logic_vector(7 downto 0) := X"00";
    signal tx_last     : std_logic := '0';
    signal tx_valid    : std_logic := '0';
    signal tx_start    : std_logic := '0';
    signal tx_next     : std_logic := '0';
    signal rx_data     : std_logic_vector(7 downto 0);
    signal status      : std_logic_vector(7 downto 0);
    signal rx_last     : std_logic;
    signal rx_valid    : std_logic;
    signal rx_store    : std_logic;
    signal rx_register : std_logic;
    
    type t_std_logic_8_vector is array (natural range <>) of std_logic_vector(7 downto 0);

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.ulpi_bus
    port map (
        clock       => clock,
        reset       => reset,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP,
        
        status      => status,
        
        -- stream interface
        tx_data     => tx_data,
        tx_last     => tx_last,
        tx_valid    => tx_valid,
        tx_start    => tx_start,
        tx_next     => tx_next,
    
        rx_data     => rx_data,
        rx_last     => rx_last,
        rx_register => rx_register,
        rx_store    => rx_store,
        rx_valid    => rx_valid );

    i_bfm: entity work.ulpi_phy_bfm
    port map (
        clock       => clock,
        reset       => reset,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP );

    p_test: process
        procedure tx_packet(invec : t_std_logic_8_vector; last : boolean) is
        begin
            wait until clock='1';
            tx_start <= '1';
            for i in invec'range loop
                tx_data  <= invec(i);
                tx_valid <= '1';
                if i = invec'right and last then
                    tx_last <= '1';
                else
                    tx_last <= '0';
                end if;
                wait until clock='1';
                tx_start <= '0';
                while tx_next = '0' loop
                    wait until clock='1';
                end loop;
            end loop;
            tx_valid <= '0';
        end procedure;
    begin
        wait for 500 ns;
        tx_packet((X"40", X"01", X"02", X"03", X"04"), true);
        wait for 300 ns;
        tx_packet((X"81", X"15"), true);
        wait for 300 ns;
        tx_packet((0 => X"C2"), false);
        wait;
    end process;

end tb;
