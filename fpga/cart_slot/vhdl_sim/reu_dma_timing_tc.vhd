library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.dma_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.slot_bus_pkg.all;

entity reu_dma_timing_tc is
end reu_dma_timing_tc;

architecture testcase of reu_dma_timing_tc is
    signal clock            : std_logic := '0';
    signal reset            : std_logic := '1';
    signal phi2_tick        : std_logic := '0';
    signal slot_req         : t_slot_req := c_slot_req_init;
    signal direct_slot_resp : t_slot_resp;
    signal delayed_slot_resp: t_slot_resp;
    signal direct_mem_req   : t_mem_req;
    signal delayed_mem_req  : t_mem_req;
    signal direct_dma_req   : t_dma_req;
    signal delayed_dma_req  : t_dma_req;
    signal direct_dma_n     : std_logic;
    signal delayed_dma_n    : std_logic;
begin
    clock <= not clock after 5 ns;
    reset <= '0' after 20 ns;

    i_direct: entity work.reu
    generic map (
        g_extended     => false,
        g_no_dma_delay => true )
    port map (
        clock       => clock,
        reset       => reset,
        slot_req    => slot_req,
        slot_resp   => direct_slot_resp,
        write_ff00  => '0',
        phi2_tick   => phi2_tick,
        reu_dma_n   => direct_dma_n,
        size_ctrl   => "111",
        enable      => '1',
        inhibit     => '0',
        mem_req     => direct_mem_req,
        mem_resp    => c_mem_resp_init,
        dma_req     => direct_dma_req,
        dma_resp    => c_dma_resp_init );

    i_delayed: entity work.reu
    generic map (
        g_extended     => false,
        g_no_dma_delay => false )
    port map (
        clock       => clock,
        reset       => reset,
        slot_req    => slot_req,
        slot_resp   => delayed_slot_resp,
        write_ff00  => '0',
        phi2_tick   => phi2_tick,
        reu_dma_n   => delayed_dma_n,
        size_ctrl   => "111",
        enable      => '1',
        inhibit     => '0',
        mem_req     => delayed_mem_req,
        mem_resp    => c_mem_resp_init,
        dma_req     => delayed_dma_req,
        dma_resp    => c_dma_resp_init );

    p_test: process
    begin
        wait until reset = '0';
        wait until rising_edge(clock);
        slot_req.io_address <= X"DF01";
        slot_req.data <= X"90";
        slot_req.io_write <= '1';

        wait until rising_edge(clock);
        slot_req.io_write <= '0';
        wait until rising_edge(clock);
        wait for 1 ns;

        assert direct_dma_n = '0'
            report "direct DMA did not stop the CPU when the command executed"
            severity failure;
        assert direct_dma_req.request = '1'
            report "direct DMA waited for phi2 after the command executed"
            severity failure;
        assert delayed_dma_n = '1' and delayed_dma_req.request = '0'
            report "cartridge DMA did not retain its initial phi2 delay"
            severity failure;

        phi2_tick <= '1';
        wait until rising_edge(clock);
        phi2_tick <= '0';
        wait until rising_edge(clock);
        wait for 1 ns;

        assert delayed_dma_n = '0' and delayed_dma_req.request = '1'
            report "cartridge DMA did not start after its initial phi2 delay"
            severity failure;

        report "** SIMULATION ENDED: SUCCESSFUL" severity note;
        wait;
    end process;
end testcase;
