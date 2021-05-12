library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.mem_bus_pkg.all;
use work.tl_flat_memory_model_pkg.all;

entity harness_c1581 is
port (
    io_irq      : out std_logic );
end entity;

architecture harness of harness_c1581 is
    signal clock        : std_logic := '0';
    
    signal reset        : std_logic := '0';
    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    
    signal iec_atn      : std_logic;
    signal iec_atn_o    : std_logic;
    signal iec_atn_i    : std_logic;
    signal iec_data     : std_logic;
    signal iec_data_o   : std_logic;
    signal iec_data_i   : std_logic;
    signal iec_clk      : std_logic;
    signal iec_clk_o    : std_logic;
    signal iec_clk_i    : std_logic;
    signal iec_fclk_o   : std_logic;
    signal iec_fclk_i   : std_logic;
    signal iec_fclk     : std_logic;

    signal mem_req      : t_mem_req_32;
    signal mem_resp     : t_mem_resp_32;

    signal act_led_n    : std_logic;
    signal audio_sample : signed(12 downto 0);

    signal tick_4MHz    : std_logic := '0';
begin
    clock <= not clock after 25 ns;
    reset <= '1', '0' after 1000 ns;
    
    process
    begin
        wait until clock = '1'; tick_4MHz <= '0';
        wait until clock = '1'; tick_4MHz <= '0';
        wait until clock = '1'; tick_4MHz <= '0';
        wait until clock = '1'; tick_4MHz <= '0';
        wait until clock = '1'; tick_4MHz <= '1';
    end process;
    
    i_io_bus_bfm: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm" )
    port map (
        clock       => clock,
    
        req         => io_req,
        resp        => io_resp );

    i_drive: entity work.c1581_drive 
    generic map (
        g_big_endian    => false,
        g_audio         => false,
        g_audio_base    => X"0010000",
        g_ram_base      => X"0000000" )
    port map (
        clock           => clock,
        reset           => reset,
        
        -- timing
        tick_4MHz       => tick_4MHz,

        -- slave port on io bus
        io_req          => io_req,
        io_resp         => io_resp,
        io_irq          => io_irq,

        -- master port on memory bus
        mem_req         => mem_req,
        mem_resp        => mem_resp,
        
        -- serial bus pins
        atn_o           => iec_atn_o, -- open drain
        atn_i           => iec_atn_i,
        clk_o           => iec_clk_o, -- open drain
        clk_i           => iec_clk_i,              
        data_o          => iec_data_o, -- open drain
        data_i          => iec_data_i,              
        fast_clk_o      => iec_fclk_o, -- open drain
        fast_clk_i      => iec_fclk_i,
        
        -- LED
        act_led_n       => act_led_n,
        
        -- audio out
        audio_sample    => audio_sample );

    iec_atn <= '0' when iec_atn_o='0' else 'Z';
    iec_atn_i <= '0' when iec_atn='0' else '1';

    iec_clk <= '0' when iec_clk_o='0' else 'Z';
    iec_clk_i <= '0' when iec_clk='0' else '1';

    iec_data <= '0' when iec_data_o='0' else 'Z';
    iec_data_i <= '0' when iec_data='0' else '1';

    iec_fclk <= '0' when iec_fclk_o='0' else 'Z';
    iec_fclk_i <= '0' when iec_fclk='0' else '1';

    i_memory: entity work.mem_bus_32_slave_bfm
    generic map(
        g_name    => "dram",
        g_latency => 2
    )
    port map(
        clock     => clock,
        req       => mem_req,
        resp      => mem_resp
    );

    iec_bfm: entity work.iec_bus_bfm
    generic map ("iec_bfm")
    port map (
        iec_clock   => iec_clk,
        iec_data    => iec_data,
        iec_atn     => iec_atn,
        iec_srq     => iec_fclk  );

end harness;
