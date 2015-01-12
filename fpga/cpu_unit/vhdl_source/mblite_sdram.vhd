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
    UART_TXD        : out   std_logic;
    UART_RXD        : in    std_logic;

    MOTOR_LEDn      : out   std_logic;
    DISK_ACTn       : out   std_logic ); 

end entity;

architecture arch of mblite_sdram is
    type t_state is (idle, mem_read, mem_write, io_access);
    signal state        : t_state;

    constant c_tag      : std_logic_vector(7 downto 0) := X"11";
    signal reset_in    : std_logic;
    signal clock       : std_logic := '1';
    signal clk_2x      : std_logic := '1';
    signal reset       : std_logic := '0';
    signal inhibit     : std_logic := '0';
    signal is_idle     : std_logic := '0';
    signal req         : t_mem_req_32 := c_mem_req_32_init;
    signal resp        : t_mem_resp_32;
    signal io_req      : t_io_req;
    signal io_resp     : t_io_resp;
    signal mmem_o      : dmem_out_type;
    signal mmem_i      : dmem_in_type;

    type t_int4_array is array(natural range <>) of integer range 0 to 3;
    --                                               0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  => 1,2,4,8 byte, 3,C word, F dword
    constant c_remain   : t_int4_array(0 to 15) := ( 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 3 );
    signal remain       : integer range 0 to 3;

begin
    process(state, resp)
    begin
        mmem_i.ena_i <= '0';
        case state is
        when idle =>
            mmem_i.ena_i <= '1';
        when mem_read | io_access =>
            mmem_i.ena_i <= '0';
        when mem_write =>
--            if resp.rack = '1' then
--                mmem_i.ena_i <= '1';
--            end if;
        when others =>
            mmem_i.ena_i <= '0';
        end case;
    end process;
    
    process(clock)
        impure function get_next_io_byte(a : unsigned(1 downto 0)) return std_logic_vector is
        begin
            case a is
            when "00" =>
                return mmem_o.dat_o(23 downto 16);
            when "01" =>
                return mmem_o.dat_o(15 downto 8);
            when "10" =>
                return mmem_o.dat_o(7 downto 0);
            when "11" =>
                return mmem_o.dat_o(31 downto 24);
            when others =>
                return "XXXXXXXX";
            end case;                            
        end function;
    begin
        if rising_edge(clock) then
            io_req.read <= '0';
            io_req.write <= '0';
            
            case state is
            when idle =>
                mmem_i.dat_i <= (others => 'X');
                if mmem_o.ena_o = '1' then
                    req.address <= unsigned(mmem_o.adr_o(req.address'range));
                    req.address(1 downto 0) <= "00";
                    req.byte_en <= mmem_o.sel_o;
                    req.data <= mmem_o.dat_o;
                    req.read_writen <= not mmem_o.we_o;
                    req.tag <= c_tag;
                    io_req.address <= unsigned(mmem_o.adr_o(19 downto 0));
                    io_req.data <= get_next_io_byte("11");
                    
                    if mmem_o.adr_o(26) = '0' then
                        req.request <= '1';
                        if mmem_o.we_o = '1' then
                            state <= mem_write;
                        else
                            state <= mem_read;
                        end if;
                    else -- I/O
                        remain <= c_remain(to_integer(unsigned(mmem_o.sel_o)));
                        if mmem_o.we_o = '1' then
                            io_req.write <= '1';
                        else
                            io_req.read <= '1';
                        end if;
                        state <= io_access;
                    end if;
                end if;

            when mem_read =>
                if resp.rack = '1' then
                    req.request <= '0';
                end if;
                if resp.dack_tag = c_tag then
                    mmem_i.dat_i <= resp.data;
                    state <= idle;
                end if;
                
            when mem_write =>
                if resp.rack = '1' then
                    req.request <= '0';
                    state <= idle;
                end if;

            when io_access =>
                case io_req.address(1 downto 0) is
                when "00" =>
                    mmem_i.dat_i(31 downto 24) <= io_resp.data;
                when "01" =>
                    mmem_i.dat_i(23 downto 16) <= io_resp.data;
                when "10" =>
                    mmem_i.dat_i(15 downto 8) <= io_resp.data;
                when "11" =>
                    mmem_i.dat_i(7 downto 0) <= io_resp.data;
                when others =>
                    null;
                end case;

                if io_resp.ack = '1' then
                    io_req.data <= get_next_io_byte(io_req.address(1 downto 0));

                    if remain = 0 then
                        state <= idle;
                    else
                        remain <= remain - 1;
                        io_req.address(1 downto 0) <= io_req.address(1 downto 0) + 1;
                        if req.read_writen = '0' then
                            io_req.write <= '1';
                        else
                            io_req.read <= '1';
                        end if;
                    end if;
                end if;

            when others =>
                null;
            end case;

            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;

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

    i_mem_ctrl: entity work.ext_mem_ctrl_v5
    generic map (
        g_simulation => false )
    port map (
        clock       => clock,
        clk_2x      => clk_2x,
        reset       => reset,
    
        inhibit     => inhibit,
        is_idle     => is_idle,
    
        req         => req,
        resp        => resp,
    
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

    i_proc: entity mblite.cached_mblite
    port map (
        clock  => clock,
        reset  => reset,
        mmem_o => mmem_o,
        mmem_i => mmem_i,
        irq_i  => '0',
        irq_o  => open );

    MOTOR_LEDn <= '1';
    DISK_ACTn  <= '0';

    i_itu: entity work.itu
    generic map (
        g_version      => X"00",
        g_uart         => true,
        g_capabilities => X"1234ABCD"
    )
    port map (
        clock          => clock,
        reset          => reset,
        io_req         => io_req,
        io_resp        => io_resp,
        irq_timer_tick => '0',
        irq_in         => "000000",
        uart_txd       => UART_TXD,
        uart_rxd       => UART_RXD );
    
end arch;
