-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : free_queue_tb
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Quick test bench for free queue
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library work;
    use work.io_bus_pkg.all;
    use work.io_bus_bfm_pkg.all;
    use work.block_bus_pkg.all;
    
entity free_queue_tb is
end entity;

architecture test of free_queue_tb is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal block_req       : t_block_req := c_block_req_init;
    signal block_resp      : t_block_resp;
    signal io_req          : t_io_req;
    signal io_resp         : t_io_resp;
    signal io_irq          : std_logic;
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.free_queue
    port map (
        clock      => clock,
        reset      => reset,
        block_req  => block_req,
        block_resp => block_resp,
        io_req     => io_req,
        io_resp    => io_resp,
        io_irq     => io_irq
    );
    
    i_bfm: entity work.io_bus_bfm
    generic map ("io")
    port map (
        clock  => clock,
        req    => io_req,
        resp   => io_resp
    );
    
    process
        variable io : p_io_bus_bfm_object;
        variable v_id : unsigned(7 downto 0);
        variable data : std_logic_vector(7 downto 0);
        
        procedure io_write_long(variable io : inout p_io_bus_bfm_object; addr : natural; data : std_logic_vector(31 downto 0)) is
        begin
            io_write(io, to_unsigned(addr+0, 20), data(07 downto 00));
            io_write(io, to_unsigned(addr+1, 20), data(15 downto 08));
            io_write(io, to_unsigned(addr+2, 20), data(23 downto 16));
            io_write(io, to_unsigned(addr+3, 20), data(31 downto 24));
        end procedure;

        procedure io_write(variable io : inout p_io_bus_bfm_object; addr : natural; data : std_logic_vector(7 downto 0)) is
        begin
            io_write(io, to_unsigned(addr, 20), data);
        end procedure;
        procedure io_read(variable io : inout p_io_bus_bfm_object; addr : natural; data : out std_logic_vector(7 downto 0)) is
        begin
            io_read(io, to_unsigned(addr, 20), data);
        end procedure;

    begin
        wait until reset = '0';
        bind_io_bus_bfm("io", io);
        io_write_long(io, 0, X"12345678");
        io_write(io, 5, X"11"); -- insert ID 11 to address 02345600

        wait until clock = '1';
        block_req.command <= allocate;
        block_req.request <= '1';
        wait until block_resp.done = '1';
        v_id := block_resp.id;
        wait until clock = '1';
        block_req.request <= '0';
        wait until clock = '1';

        wait until clock = '1';
        block_req.command <= write;
        block_req.id <= v_id;
        block_req.request <= '1';
        block_req.bytes <= X"ABC";
        wait until block_resp.done = '1';
        wait until clock = '1';
        block_req.request <= '0';
        wait until clock = '1';

        io_read(io, 15, data);
        assert data(0) = '1' report "Expected an allocated block to be available";
        io_read(io, 8, data);
        assert data = X"11" report "Expected allocated block to have ID 11";
        io_write(io, 15, X"00");
        io_write_long(io, 0, X"DEADBABE");
        io_write(io, 4, X"11"); -- free block 11

        io_read(io, 15, data);
        assert data(0) = '0' report "Expected no more allocated blocks to be available";

        wait until clock = '1';
        block_req.command <= allocate;
        block_req.request <= '1';
        wait until block_resp.done = '1';
        v_id := block_resp.id;
        wait until clock = '1';
        block_req.request <= '0';
        wait until clock = '1';

        wait until clock = '1';
        block_req.command <= allocate;
        block_req.request <= '1';
        wait until block_resp.done = '1';
        v_id := block_resp.id;
        wait until clock = '1';
        block_req.request <= '0';
        wait until clock = '1';

        wait;
    end process;
    
end architecture;
