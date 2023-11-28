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

entity tb_uart_dma is

end entity;

architecture testcase of tb_uart_dma is
    signal clock           : std_logic := '0'; -- 50 MHz reference clock
    signal reset           : std_logic;

    signal io_req          : t_io_req;
    signal io_resp         : t_io_resp := c_io_resp_init;
    signal io_irq          : std_logic;

    signal mem_req            : t_mem_req_32;
    signal mem_resp           : t_mem_resp_32 := c_mem_resp_32_init;

    signal rxd, txd, rts, cts : std_logic := '1';

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

    uart_dma_inst: entity work.uart_dma
        generic map (
            g_rx_tag  => X"01",
            g_tx_tag  => X"02",
            g_divisor => 15
        )
        port map (
            clock    => clock,
            reset    => reset,
            io_req   => io_req,
            io_resp  => io_resp,
            irq      => io_irq,
            mem_req  => mem_req,
            mem_resp => mem_resp,
            txd      => txd,
            rxd      => rxd,
            rts      => rts,
            cts      => cts
        );

    rxd <= txd;
    cts <= rts;

    test: process
        variable mm : h_mem_object;
        variable io : p_io_bus_bfm_object;
        variable datab : std_logic_vector(7 downto 0);
        variable dataw : std_logic_vector(15 downto 0);

        procedure expect_packet(size: natural) is
        begin
            -- Wait for Rx IRQ
            wait until io_irq = '1';
            io_read_word(io, 8, dataw);
            assert to_integer(unsigned(dataw)) = size report "Expected packet with " & integer'image(size) & " bytes" severity error;
            io_read(io, 2, datab);
            assert datab(0) = '1' report "Expected RxPacket available to be set" severity error;
            -- Release received packet
            io_write(io, 11, X"01");
            io_read(io, 2, datab);
            assert datab(0) = '0' report "Expected RxPacket available to be clear" severity error;
        end procedure;
    begin
        bind_io_bus_bfm("io", io);
        bind_mem_model("my_memory", mm);
        load_memory("makefile", mm, X"00000000");
        write_memory_32(mm, X"00001020", X"C0DBDDC0");
        wait until reset = '0';
        -- Enable slip and flow control
        io_write(io, 3, X"05");
        -- Enable rx interrupt
        io_write(io, 2, X"81");
        -- Set receive buffer to address 10000
        io_write_long(io, 12, X"00010000");
        -- send a packet of 50 bytes from address 4K
        io_write_long(io, 4, X"00001000");
        io_write_word(io, 8, X"0032");
        io_write(io, 10, X"01");
        expect_packet(50);

        -- Now let's try to send two packets in a row, 40 and 300 bytes resp
        io_write_word(io, 8, X"0028");
        io_write(io, 10, X"01");
        for i in 0 to 100 loop
            io_read(io, 2, datab);
            if datab(1) = '1' then
                exit;
            end if;
        end loop;
        io_write_word(io, 8, X"022C");
        io_write(io, 10, X"01");

        -- Now lets try to receive two packets in a row
        wait for 2000 us;
        io_write_long(io, 12, X"00020000");
        expect_packet(40);
        io_write_long(io, 12, X"00030000");
        expect_packet(16#22c#);

        wait;
    end process;

    i_bfm: entity work.io_bus_bfm
    generic map ("io")
    port map (
        clock  => clock,
        req    => io_req,
        resp   => io_resp
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
