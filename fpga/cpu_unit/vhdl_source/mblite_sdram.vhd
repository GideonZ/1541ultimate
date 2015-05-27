--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: mblite_sdram
-- Date:2015-01-02  
-- Author: Gideon     
-- Description: mblite processor with sdram interface - test module
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    
library mblite;
    use mblite.core_Pkg.all;

library std;
    use std.textio.all;

library work;
    use work.tl_string_util_pkg.all;

entity mblite_sdram is
port (
    CLOCK_50        : in    std_logic;

    SDRAM_CLK       : out   std_logic;
    SDRAM_CKE       : out   std_logic;
    SDRAM_CSn       : out   std_logic := '1';
    SDRAM_RASn      : out   std_logic := '1';
    SDRAM_CASn      : out   std_logic := '1';
    SDRAM_WEn       : out   std_logic := '1';
    SDRAM_DQM       : out   std_logic := '0';
    SDRAM_A         : out   std_logic_vector(12 downto 0);
    SDRAM_BA        : out   std_logic_vector(1 downto 0);
    SDRAM_DQ        : inout std_logic_vector(7 downto 0) := (others => 'Z');

    BUTTON          : in    std_logic_vector(0 to 2);

    MOTOR_LEDn      : out   std_logic;
    DISK_ACTn       : out   std_logic ); 

end entity;

architecture arch of mblite_sdram is

    constant c_tag_i    : std_logic_vector(7 downto 0) := X"10";
    constant c_tag_d    : std_logic_vector(7 downto 0) := X"11";
    constant c_tag_test : std_logic_vector(7 downto 0) := X"91";
    signal reset_in    : std_logic;
    signal clock       : std_logic := '1';
    signal clk_2x      : std_logic := '1';
    signal reset       : std_logic := '0';
    signal inhibit     : std_logic := '0';
    signal is_idle     : std_logic := '0';
    signal irq         : std_logic;
    
    signal mem_req_32_cpu   : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_cpu  : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_test  : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_test : t_mem_resp_32 := c_mem_resp_32_init;
    
    signal mem_req     : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp    : t_mem_resp_32;
    signal io_req      : t_io_req;
    signal io_resp     : t_io_resp;
    signal txd, rxd     : std_logic := '1';
    signal invalidate      : std_logic := '0';
    signal inv_addr        : std_logic_vector(31 downto 0) := (others => '0');

begin
    reset_in <= not BUTTON(1);

    i_clk: entity work.s3a_clockgen
    port map (
        clk_50       => CLOCK_50,
        reset_in     => '0',
        
        dcm_lock     => open,
        soft_reset   => reset_in,
        
        sys_clock    => clock, -- 50 MHz
        sys_reset    => reset,
        sys_clock_2x => clk_2x );


    i_proc: entity work.mblite_wrapper
    generic map (
        g_tag_i    => c_tag_i,
        g_tag_d    => c_tag_d )
    port map(
        clock      => clock,
        reset      => reset,
        irq_i      => irq,
        irq_o      => open,
        invalidate => invalidate,
        inv_addr   => inv_addr,
        io_req     => io_req,
        io_resp    => io_resp,
        mem_req    => mem_req_32_cpu,
        mem_resp   => mem_resp_32_cpu   );

    invalidate <= '1' when (mem_resp_32_test.rack_tag = c_tag_test) and (mem_req_32_test.read_writen = '0') else '0';
    inv_addr(31 downto 26) <= (others => '0');
    inv_addr(25 downto 0) <= std_logic_vector(mem_req_32_test.address);


    i_mem_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_ports      => 2,
        g_registered => false )
    port map (
        clock       => clock,
        reset       => reset,
        
        reqs(0)     => mem_req_32_test,
        reqs(1)     => mem_req_32_cpu,

        resps(0)    => mem_resp_32_test,
        resps(1)    => mem_resp_32_cpu,
        
        req         => mem_req,
        resp        => mem_resp );        

    i_mem_ctrl: entity work.ext_mem_ctrl_v5
    generic map (
        g_simulation => false )
    port map (
        clock       => clock,
        clk_2x      => clk_2x,
        reset       => reset,
    
        inhibit     => inhibit,
        is_idle     => is_idle,
    
        req         => mem_req,
        resp        => mem_resp,
    
        SDRAM_CLK   => SDRAM_CLK,
        SDRAM_CKE   => SDRAM_CKE,
        SDRAM_CSn   => SDRAM_CSn,
        SDRAM_RASn  => SDRAM_RASn,
        SDRAM_CASn  => SDRAM_CASn,
        SDRAM_WEn   => SDRAM_WEn,
        SDRAM_DQM   => SDRAM_DQM,
    
        SDRAM_BA    => SDRAM_BA,
        SDRAM_A     => SDRAM_A,
        SDRAM_DQ    => SDRAM_DQ );

    MOTOR_LEDn <= '1';
    DISK_ACTn  <= '0';

    i_itu: entity work.itu
    generic map (
        g_version      => X"00",
        g_uart         => true,
        g_baudrate     => 5_000_000,
        g_capabilities => X"80000000"
    )
    port map (
        clock          => clock,
        reset          => reset,
        io_req         => io_req,
        io_resp        => io_resp,
        irq_timer_tick => '0',
        irq_in         => "000000",
        irq_out        => irq,
        uart_txd       => txd,
        uart_rxd       => rxd );

    -- IO
    process(clock)
        variable s    : line;
        variable char : character;
        variable byte : std_logic_vector(7 downto 0);
    begin
        if rising_edge(clock) then
            if io_req.write = '1' then -- write
                case io_req.address is
                when X"00010" => -- UART_DATA
                    byte := io_req.data;
                    char := character'val(to_integer(unsigned(byte)));
                    if byte = X"0D" then
                        -- Ignore character 13
                    elsif byte = X"0A" then
                        -- Writeline on character 10 (newline)
                        writeline(output, s);
                    else
                        -- Write to buffer
                        write(s, char);
                    end if;
                when others =>
                    null;
                end case;
            end if;
        end if;
    end process;
    

    process
        --constant c_test : std_logic_vector(1 to 10) := "1111111110";
    begin
        mem_req_32_test.address <= to_unsigned(16#40000#, mem_req_32_test.address'length);
        mem_req_32_test.tag <= c_tag_test;

        wait;
        wait for 600 us;
        for i in 1 to 10000 loop
            wait until clock = '1';
            mem_req_32_test.byte_en <= "1111";
            mem_req_32_test.data <= X"12345678";
            mem_req_32_test.read_writen <= '0'; --c_test(i);
            mem_req_32_test.request <= '1';
            L1: while true loop
                wait until clock = '1';
                if mem_resp_32_test.rack_tag = c_tag_test then
                    exit L1;
                end if;
            end loop;
            mem_req_32_test.request <= '0';
            mem_req_32_test.address <= mem_req_32_test.address + 0;
            wait until clock = '1';
            wait until clock = '1';
        end loop;
        wait;
    end process;
end arch;
