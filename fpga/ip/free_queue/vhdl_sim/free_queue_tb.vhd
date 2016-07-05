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
    signal alloc_req       : std_logic := '0';
    signal alloc_resp      : t_alloc_resp;
    signal used_req        : t_used_req;
    signal used_resp       : std_logic;
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
        alloc_req  => alloc_req,
        alloc_resp => alloc_resp,
        used_req   => used_req,
        used_resp  => used_resp,
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
        variable data16 : std_logic_vector(15 downto 0);
        
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
        procedure io_read_word(variable io : inout p_io_bus_bfm_object; addr : natural; data : out std_logic_vector(15 downto 0)) is
        begin
            io_read(io, to_unsigned(addr, 20), data(7 downto 0));
            io_read(io, to_unsigned(addr+1, 20), data(15 downto 8));
        end procedure;

    begin
        wait until reset = '0';
        bind_io_bus_bfm("io", io);
        io_write_long(io, 0, X"12345678");
        io_write(io, 4, X"11"); -- insert ID 11

        wait until clock = '1';
        alloc_req <= '1';
        wait until alloc_resp.done = '1';
        v_id := alloc_resp.id;
        assert(to_integer(alloc_resp.address) = (16#2345600# + 17 * 1536)) report "Unexpected address";
        wait until clock = '1';
        alloc_req <= '0';
        wait until clock = '1';

        wait until clock = '1';
        used_req.request <= '1';
        used_req.id <= v_id;
        used_req.bytes <= X"ABC";
        wait until used_resp = '1';
        wait until clock = '1';
        used_req.request <= '0';
        wait until clock = '1';
        wait until clock = '1';
        wait until clock = '1';

        io_read(io, 15, data);
        assert data(0) = '1' report "Expected an allocated block to be available";
        io_read(io, 8, data);
        assert data = X"11" report "Expected allocated block to have ID 11";
        io_read_word(io, 10, data16);
        assert data16 = X"0ABC" report "Expected length to be 0xABC";
        io_write(io, 15, X"00"); -- pop!
        io_read(io, 15, data);
        assert data(0) = '0' report "Expected no more allocated blocks to be available";


        io_write(io, 4, X"00"); -- add free block 00

        wait until clock = '1';
        alloc_req <= '1';
        wait until alloc_resp.done = '1';
        v_id := alloc_resp.id;
        assert(to_integer(alloc_resp.address) = (16#2345600# + 0 * 1536)) report "Unexpected address";
        wait until clock = '1';
        alloc_req <= '0';
        wait until clock = '1';

        wait until clock = '1';
        alloc_req <= '1';
        wait until alloc_resp.done = '1';
        v_id := alloc_resp.id;
        assert(alloc_resp.error = '1') report "Allocation should now fail";
        wait until clock = '1';
        alloc_req <= '0';
        wait until clock = '1';

        wait;
    end process;
    
end architecture;
