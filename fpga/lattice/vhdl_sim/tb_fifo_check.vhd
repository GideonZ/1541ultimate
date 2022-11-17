library ieee;
use ieee.std_logic_1164.all;
library ecp5u;
use ecp5u.components.all;

entity tb_fifo_check is
end entity;

architecture tb of tb_fifo_check is
    signal command : std_logic_vector(7 downto 0);
    signal command_fifo_dout : std_logic_vector(8 downto 0);
    signal completion : std_logic;
    signal sys_clock : std_logic := '0';
    signal command_fifo_push : std_logic;
    signal command_fifo_pop : std_logic;
    signal drv_reset : std_logic;
    signal command_fifo_valid : std_logic;
    signal my_net : std_logic;
begin
    PUR_INST: entity work.P port map (PUR => my_net);
    GSR_INST: entity work.G port map (GSR => my_net);
    my_net <= '0', '1' after 50 ns;

    sys_clock <= not sys_clock after 10 ns;
    command <= X"44";
    completion <= '1';
    process
    begin
        command_fifo_push <= '0';
        command_fifo_pop <= '0';
        drv_reset <= '1';
        wait for 100 ns;
        drv_reset <= '0';
        wait until sys_clock = '1';
        command_fifo_push <= '1';
        wait until sys_clock = '1';
        command_fifo_push <= '0';
        wait until sys_clock = '1';
        wait until sys_clock = '1';
        drv_reset <= '1';
        wait until sys_clock = '1';
        drv_reset <= '0';
        wait until sys_clock = '1';
        wait until sys_clock = '1';
        wait until sys_clock = '1';
        wait until sys_clock = '1';
        drv_reset <= '1';
        command_fifo_push <= '1';
        wait until sys_clock = '1';
        wait until sys_clock = '1';
        drv_reset <= '0';
        command_fifo_push <= '0';
        wait;
    end process;

    i_dut: entity work.sync_fifo_uniq_3_7_9_4_MRAM_block_ram_true
    port map(
        command           => command,
        command_fifo_dout => command_fifo_dout,
        completion        => completion,
        sys_clock         => sys_clock,
        command_fifo_push => command_fifo_push,
        command_fifo_pop  => command_fifo_pop,
        drv_reset         => drv_reset,
        reveal_ist_452    => command_fifo_valid
    );
    
end architecture;
