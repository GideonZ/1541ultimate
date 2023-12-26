--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: Testbench for the DMA controllers. They are placed back to back
--              to copy data from one area in the memory to another.
--------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
   
library work;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.mem_bus_pkg.all;
use work.stream_source_pkg.all;
use work.tl_flat_memory_model_pkg.all;

entity tb_dma is

end entity;

architecture testcase of tb_dma is
    signal clock           : std_logic := '0'; -- 50 MHz reference clock
    signal reset           : std_logic;

    signal io_req          : t_io_req;
    signal io_resp         : t_io_resp := c_io_resp_init;
    signal io_irq          : std_logic;

    signal tx_mem_req         : t_mem_req_32;
    signal tx_mem_resp        : t_mem_resp_32 := c_mem_resp_32_init;
    signal rx_mem_req         : t_mem_req_32;
    signal rx_mem_resp        : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req            : t_mem_req_32;
    signal mem_resp           : t_mem_resp_32 := c_mem_resp_32_init;

    signal tx_addr_data       : std_logic_vector(27 downto 0) := (others => '0');
    signal tx_addr_user       : std_logic_vector(15 downto 0) := (others => '0'); -- Length
    signal tx_addr_valid      : std_logic := '0';
    signal tx_addr_ready      : std_logic;
    signal out_data           : std_logic_vector(7 downto 0) := X"00";
    signal out_valid          : std_logic;
    signal out_last           : std_logic;
    signal out_full           : std_logic;
    signal out_ready          : std_logic;
    signal in_data            : std_logic_vector(7 downto 0) := X"00";
    signal in_valid           : std_logic;
    signal in_last            : std_logic;
    signal in_ready           : std_logic;
    signal rx_addr_data       : std_logic_vector(27 downto 0) := (others => '0');
    signal rx_addr_valid      : std_logic := '0';
    signal rx_addr_ready      : std_logic;
    signal rx_len_data        : std_logic_vector(15 downto 0);
    signal rx_len_valid       : std_logic;
    signal rx_len_ready       : std_logic := '1';

    type t_byte_array is array (natural range <>) of std_logic_vector(7 downto 0);

    procedure io_write_long(variable io : inout p_io_bus_bfm_object; addr : natural; data : std_logic_vector(31 downto 0)) is
    begin
        io_write(io, to_unsigned(addr+0, 24), data(07 downto 00));
        io_write(io, to_unsigned(addr+1, 24), data(15 downto 08));
        io_write(io, to_unsigned(addr+2, 24), data(23 downto 16));
        io_write(io, to_unsigned(addr+3, 24), data(31 downto 24));
    end procedure;

    procedure io_write_word(variable io : inout p_io_bus_bfm_object; addr : natural; data : std_logic_vector(15 downto 0)) is
    begin
        io_write(io, to_unsigned(addr+0, 24), data(07 downto 00));
        io_write(io, to_unsigned(addr+1, 24), data(15 downto 08));
    end procedure;

    procedure io_write(variable io : inout p_io_bus_bfm_object; addr : natural; data : std_logic_vector(7 downto 0)) is
    begin
        io_write(io, to_unsigned(addr, 24), data);
    end procedure;
    procedure io_read(variable io : inout p_io_bus_bfm_object; addr : natural; data : out std_logic_vector(7 downto 0)) is
    begin
        io_read(io, to_unsigned(addr, 24), data);
    end procedure;
    procedure io_read_word(variable io : inout p_io_bus_bfm_object; addr : natural; data : out std_logic_vector(15 downto 0)) is
    begin
        io_read(io, to_unsigned(addr, 24), data(7 downto 0));
        io_read(io, to_unsigned(addr+1, 24), data(15 downto 8));
    end procedure;

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    tx_addr_source: entity work.stream_source_bfm
        generic map (
            g_name       => "tx_addr",
            g_data_width => 32,
            g_user_width => 16
        )
        port map (
            clock => clock,
            reset => reset,
            data  => tx_addr_data,
            user  => tx_addr_user,
            valid => tx_addr_valid,
            ready => tx_addr_ready
        );

    tx_dma_inst: entity work.tx_dma
        generic map (
            g_mem_tag => X"01"
        )
        port map (
            clock      => clock,
            reset      => reset,
            addr_data  => tx_addr_data,
            addr_user  => tx_addr_user,
            addr_valid => tx_addr_valid,
            addr_ready => tx_addr_ready,
            mem_req    => tx_mem_req,
            mem_resp   => tx_mem_resp,
            out_data   => out_data,
            out_valid  => out_valid,
            out_last   => out_last,
            out_ready  => out_ready
        );

    sync_fifo_inst: entity work.sync_fifo
        generic map (
            g_depth        => 15,
            g_data_width   => 9,
            g_threshold    => 8,
            g_fall_through => true
        )
        port map (
            clock       => clock,
            reset       => reset,
            din(7 downto 0)  => out_data,
            din(8)           => out_last,
            wr_en       => out_valid,
            full        => out_full,

            dout(7 downto 0) => in_data,
            dout(8)          => in_last,
            rd_en       => in_ready,
            valid       => in_valid
        );
    out_ready <= not out_full;

    rx_addr_source: entity work.stream_source_bfm
        generic map (
            g_name       => "rx_addr",
            g_data_width => 32,
            g_user_width => 0
        )
        port map (
            clock => clock,
            reset => reset,
            data  => rx_addr_data,
            valid => rx_addr_valid,
            ready => rx_addr_ready
        );

    rx_dma_inst: entity work.rx_dma
        generic map (
            g_mem_tag => X"02"
        )
        port map (
            clock      => clock,
            reset      => reset,
            addr_data  => rx_addr_data,
            addr_valid => rx_addr_valid,
            addr_ready => rx_addr_ready,
            len_data   => rx_len_data,
            len_valid  => rx_len_valid,
            len_ready  => rx_len_ready,
            mem_req    => rx_mem_req,
            mem_resp   => rx_mem_resp,
            in_data    => in_data,
            in_valid   => in_valid,
            in_last    => in_last,
            in_ready   => in_ready
        );

    test: process
        variable tx : h_stream_source;
        variable rx : h_stream_source;
        variable mm : h_mem_object;
    begin
        bind_stream_source("tx_addr", tx);
        bind_stream_source("rx_addr", rx);
        bind_mem_model("my_memory", mm);
        load_memory("makefile", mm, X"00000000");
        put(rx, X"0010000");
        put(rx, X"0020000");
        put(tx, X"0001001", X"0010");
        put(tx, X"0002002", X"0012");
        wait;
    end process;

    -- test: process    
    --     variable io : p_io_bus_bfm_object;
    --     variable data : std_logic_vector(7 downto 0);
    --     --variable i : natural;
    -- begin
    --     wait until reset = '0';
    --     bind_io_bus_bfm("io", io);
    --     io_write_long(io, 0, X"01200000");
    --     io_write_word(io, 4, X"0010");
    --     io_write(io, 8, X"00"); -- start
    --     wait until io_irq = '1';
    --     io_write(io, 9, X"00"); -- clear IRQ
    --     io_write_long(io, 0, X"01200003");
    --     io_write_word(io, 4, X"0011");
    --     io_write(io, 8, X"00"); -- start
    --     wait until io_irq = '1';
    --     io_write(io, 9, X"00"); -- clear IRQ
    --     io_write_long(io, 0, X"02200002");
    --     io_write_word(io, 4, X"0700");
    --     io_write(io, 8, X"00"); -- start
    --     wait until io_irq = '1';
    --     io_write(io, 9, X"00"); -- clear IRQ
    --     wait;
    -- end process;

    -- i_bfm: entity work.io_bus_bfm
    -- generic map ("io")
    -- port map (
    --     clock  => clock,
    --     req    => io_req,
    --     resp   => io_resp
    -- );

    mem_bus_arbiter_pri_32_inst: entity work.mem_bus_arbiter_pri_32
        generic map (
            g_registered => false,
            g_ports      => 2
        )
        port map (
            clock   => clock,
            reset   => reset,
            inhibit => '0',
            reqs(1) => tx_mem_req,
            reqs(0) => rx_mem_req,
            resps(1) => tx_mem_resp,
            resps(0) => rx_mem_resp,
            req     => mem_req,
            resp    => mem_resp
        );

    mem_bus_32_slave_bfm_inst: entity work.mem_bus_32_slave_bfm
        generic map (
            g_name        => "my_memory",
            g_time_to_ack => 0,
            g_latency     => 5
        )
        port map (
            clock => clock,
            req   => mem_req,
            resp  => mem_resp
        );

end architecture;
