library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_ulpi_rx is

end entity;

architecture tb of tb_ulpi_rx is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;

    signal rx_data         : std_logic_vector(7 downto 0) := X"00";
    signal rx_last         : std_logic := '0';
    signal rx_valid        : std_logic := '0';
    signal rx_store        : std_logic := '0';

    signal pid             : std_logic_vector(3 downto 0);
    signal valid_token     : std_logic;
    signal token           : std_logic_vector(10 downto 0);
    signal valid_packet    : std_logic;
    signal data_out        : std_logic_vector(7 downto 0);
    signal data_valid      : std_logic;
    signal data_start      : std_logic;
    signal error           : std_logic;

    type t_std_logic_8_vector is array (natural range <>) of std_logic_vector(7 downto 0);
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_rx: entity work.ulpi_rx
    port map (
        clock           => clock,
        reset           => reset,
                        
        rx_data         => rx_data,
        rx_last         => rx_last,
        rx_valid        => rx_valid,
        rx_store        => rx_store,
                        
        pid             => pid,
        valid_token     => valid_token,
        token           => token,
    
        valid_packet    => valid_packet,
        data_out        => data_out,
        data_valid      => data_valid,
        data_start      => data_start,
        
        error           => error );

    process
        procedure packet(pkt : t_std_logic_8_vector) is
        begin
            for i in pkt'range loop
                wait until clock='1';
                rx_data <= pkt(i);
                rx_valid <= '1';
                rx_store <= '1';
                if i = pkt'right then
                    rx_last <= '1';
                else
                    rx_last <= '0';
                end if;
            end loop;
            wait until clock='1';
            rx_valid <= '0';
            rx_last  <= '0';
            wait until clock='1';
            wait until clock='1';
            wait until clock='1';
        end procedure packet;
    begin
        wait until reset='0';
        wait until clock='1';
        packet((X"A5", X"63", X"A9"));
        packet((X"4B", X"00", X"00")); -- data1, length=0, crc = 0000
        packet((X"C3", X"00", X"01", X"02", X"03", X"04", X"05", X"06",
                X"07", X"08", X"09", X"0A", X"0B", X"0C", X"0D", X"0E",
                X"0F", X"10", X"19", X"44")); -- good crc
        packet((X"C3", X"00", X"01", X"02", X"03", X"04", X"05", X"06",
                X"07", X"08", X"09", X"03", X"0B", X"0C", X"0D", X"0E",
                X"0F", X"10", X"19", X"44")); -- bad crc
        packet((0=>X"D2")); -- good handshake
        packet((0=>X"C3")); -- bad handshake (wrong pid data)
        packet((0=>X"A5")); -- bad handshake (wrong pid token)
        wait;
    end process;
end tb;
