library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;

entity tb_ulpi_tx is

end entity;

architecture tb of tb_ulpi_tx is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal ULPI_DATA   : std_logic_vector(7 downto 0);
    signal ULPI_DIR    : std_logic;
    signal ULPI_NXT    : std_logic;
    signal ULPI_STP    : std_logic;
 
    signal tx_data     : std_logic_vector(7 downto 0) := X"40";
    signal tx_last     : std_logic := '0';
    signal tx_valid    : std_logic := '1';
    signal tx_start    : std_logic := '0';
    signal tx_next     : std_logic := '0';

    signal rx_data     : std_logic_vector(7 downto 0) := X"00";
    signal rx_command  : std_logic;
    signal rx_register : std_logic;
    signal rx_last     : std_logic;
    signal rx_valid    : std_logic;

    signal status      : std_logic_vector(7 downto 0);
    signal busy        : std_logic;

    -- Interface to send tokens
    signal send_handsh : std_logic := '0';
    signal send_token  : std_logic := '0';
    signal pid         : std_logic_vector(3 downto 0) := X"0";
    signal token       : std_logic_vector(10 downto 0) := (others => '0');
    
    -- Interface to send data packets
    signal send_data   : std_logic := '0';
    signal user_data   : std_logic_vector(7 downto 0) := X"00";
    signal user_last   : std_logic := '0';
    signal user_valid  : std_logic := '0';
    signal user_next   : std_logic := '0';
            
    -- Interface to read/write registers
    signal read_reg    : std_logic := '0';
    signal write_reg   : std_logic := '0';
    signal address     : std_logic_vector(5 downto 0) := (others => '0');
    signal write_data  : std_logic_vector(7 downto 0) := (others => '0');
    signal read_data   : std_logic_vector(7 downto 0) := (others => '0');
    
    type t_std_logic_8_vector is array (natural range <>) of std_logic_vector(7 downto 0);

    signal set         : std_logic_vector(7 downto 0) := X"00";

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_tx: entity work.ulpi_tx
    port map (
        clock       => clock,
        reset       => reset,
        
        -- Bus Interface
        tx_start    => tx_start,
        tx_last     => tx_last,
        tx_valid    => tx_valid,
        tx_next     => tx_next,
        tx_data     => tx_data,
        rx_register => rx_register,
        rx_data     => rx_data,
        
        -- Status
        busy        => busy,
        
        -- Interface to send tokens
        send_token  => send_token,
        send_handsh => send_handsh,
        pid         => pid,
        token       => token,
        
        -- Interface to send data packets
        send_data   => send_data,
        user_data   => user_data,
        user_last   => user_last,
        user_valid  => user_valid,
        user_next   => user_next,
                
        -- Interface to read/write registers
        read_reg    => read_reg,
        write_reg   => write_reg,
        address     => address,
        write_data  => write_data,
        read_data   => read_data );

    
    i_bus: entity work.ulpi_bus
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
        rx_register => rx_register,
        rx_last     => rx_last,
        rx_valid    => rx_valid );

    i_bfm: entity work.ulpi_phy_bfm
    generic map (
        g_rx_interval => 500 )
    port map (
        clock       => clock,
        reset       => reset,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP );

    P_data: process(clock)
    begin
        if rising_edge(clock) then
            if set /= X"00" then
                user_data <= set;
            elsif user_next='1' then
                user_data <= std_logic_vector(unsigned(tx_data) + 1);
            end if;
        end if;
    end process;

    p_test: process
    begin
        wait until reset='0';
        wait until clock='1';
        write_data <= X"21";
        address <= "010101";
        write_reg <= '1';
        wait until clock='1';
        write_reg <= '0';        
        wait until busy='0';

        wait until clock='1';
        address <= "101010";
        read_reg <= '1';
        wait until clock='1';
        read_reg <= '0';        
        wait until busy='0';

        wait until clock='1';
        pid <= c_pid_sof;
        token <= "00101100011";
        send_token <= '1';
        wait until clock='1';
        send_token <= '0';
        wait until busy='0';

        wait until clock='1';
        send_data <= '1';        
        pid <= c_pid_data0;
        wait until clock='1';
        send_data <= '0';        
        wait until user_data = X"10";
        user_last <= '1';
        wait until clock = '1';
        user_last <= '0';
        
        wait until busy='0';
        wait until clock='1';
        pid <= c_pid_ack;
        token <= "00101100011";
        send_handsh <= '1';
        wait until clock='1';
        send_handsh <= '0';

        wait;
    end process;

end tb;
