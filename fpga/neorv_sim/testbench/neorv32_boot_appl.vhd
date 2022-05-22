library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
    use work.tl_flat_memory_model_pkg.all;
    
entity neorv32_boot_appl is
end entity;

architecture test of neorv32_boot_appl is
    signal sys_clock  : std_logic := '0';
    signal sys_reset   : std_logic;

    signal uart_txd_o : std_ulogic; -- UART0 send data
    signal uart_rxd_i : std_ulogic := '0'; -- UART0 receive data

    signal io_irq       : std_logic;
    signal io_req_riscv : t_io_req;
    signal io_resp_riscv: t_io_resp;
    signal cpu_mem_req      : t_mem_req_32;
    signal cpu_mem_resp     : t_mem_resp_32;

    -- Timing
    signal tick_16MHz       : std_logic;
    signal tick_4MHz        : std_logic;
    signal tick_1MHz        : std_logic;
    signal tick_1kHz        : std_logic;    

begin
    sys_clock <= not sys_clock after 10 ns;
    sys_reset <= '1', '0' after 100 ns;
    
    i_riscv: entity work.neorv32_wrapper
    generic map (
        g_jtag_debug=> false,
        g_frequency => 50_000_000,
        g_tag       => X"20"
    )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        cpu_reset   => '0',

        jtag_trst_i => '1',
        jtag_tck_i  => '0',
        jtag_tdi_i  => '0',
        jtag_tms_i  => '0',
        jtag_tdo_o  => open,

        irq_i       => io_irq,
        irq_o       => open,
        io_req      => io_req_riscv,
        io_resp     => io_resp_riscv,
        io_busy     => open,
        mem_req     => cpu_mem_req,
        mem_resp    => cpu_mem_resp
    );

    i_timing: entity work.fractional_div
    generic map(
        g_numerator   => 8,
        g_denominator => 25
    )
    port map(
        clock         => sys_clock,
        tick          => tick_16MHz,
        tick_by_4     => tick_4MHz,
        tick_by_16    => tick_1MHz,
        one_16000     => tick_1kHz
    );

    i_itu: entity work.itu
    generic map(
        g_version      => X"33",
        g_uart         => true,
        g_uart_rx      => false,
        g_edge_init    => "10000101",
        g_capabilities => X"80000000",
        g_edge_write   => false,
        g_baudrate     => 5_000_000
    )
    port map (
        clock          => sys_clock,
        reset          => sys_reset,
        io_req         => io_req_riscv,
        io_resp        => io_resp_riscv,
        irq_out        => io_irq,
        tick_4MHz      => tick_4MHz,
        tick_1us       => tick_1MHz,
        tick_1ms       => tick_1kHz,
        buttons        => "111",
        irq_timer_tick => open,
        irq_in         => "000000",
        irq_flags      => open,
        irq_high       => "00000000",
        busy_led       => open,
        misc_io        => open,
        uart_txd       => uart_txd_o,
        uart_rxd       => uart_rxd_i,
        uart_rts       => open,
        uart_cts       => '1'
    );
    
    i_mem: entity work.mem_bus_32_slave_bfm
    generic map (
        g_name    => "ram",
        g_time_to_ack => 0,
        g_latency => 1
    )
    port map(
        clock     => sys_clock,
        req       => cpu_mem_req,
        resp      => cpu_mem_resp
    );
    
    p_init: process
        variable mem: h_mem_object; 
    begin
        wait for 1 ns;
        bind_mem_model("ram", mem);
        --load_memory("../../target/software/riscv32_ultimate/result/ultimate.bin", mem, X"00030000" );
        load_memory("../../target/software/riscv32_rtos/result/free_rtos_demo.bin", mem, X"00030000" );
        wait;
    end process;
    
end architecture;
