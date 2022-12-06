library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity neorv32_boot_tb is
end entity;

architecture test of neorv32_boot_tb is
    signal clk_i      : std_logic := '0';
    signal rstn_i     : std_logic;
    signal gpio_o     : std_ulogic_vector(3 downto 0);
    signal uart_txd_o : std_ulogic; -- UART0 send data
    signal uart_rxd_i : std_ulogic := '0'; -- UART0 receive data
    signal uart_rts_o : std_ulogic; -- hw flow control: UART0.RX ready to receive ("RTR"), low-active, optional
    signal pwm_o      : std_ulogic_vector(2 downto 0);

begin
    clk_i <= not clk_i after 10 ns;
    rstn_i <= '0', '1' after 100 ns;
    
    i_dut: entity work.neorv32_ProcessorTop_MinimalBoot
    generic map(
        CLOCK_FREQUENCY              => 50_000_000
    )
    port map (
        clk_i                        => clk_i,
        rstn_i                       => rstn_i,
        gpio_o                       => gpio_o,
        uart_txd_o                   => uart_txd_o,
        uart_rxd_i                   => uart_rxd_i,
        uart_rts_o                   => uart_rts_o,
        pwm_o                        => pwm_o
    );

end architecture;
