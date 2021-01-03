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
    signal used_req        : t_used_req := c_used_req_init;
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
        variable v_id : integer;
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

        procedure alloc is
        begin
            wait until clock = '1';
            alloc_req <= '1';
            wait until clock = '1';
            while alloc_resp.done = '0' loop
                wait until clock = '1';
            end loop;
            alloc_req <= '0';
        end procedure;

        procedure free(id : natural) is
        begin
            io_write(io, 4, std_logic_vector(to_unsigned(id, 8)));
        end procedure;

        procedure reset_queue is
        begin
            io_write(io, 14, X"01");
        end procedure;

        procedure used(id : natural; size: unsigned(11 downto 0)) is
        begin
            wait until clock = '1';
            used_req.request <= '1';
            used_req.id <= to_unsigned(id, 8);
            used_req.bytes <= size;
            wait until clock = '1';
            while used_resp = '0' loop
                wait until clock = '1';
            end loop;
            used_req.request <= '0';
        end procedure;
    begin
        wait until reset = '0';
        bind_io_bus_bfm("io", io);
        io_write_long(io, 0, X"12345678");
        
        free(17);

        alloc;
        v_id := to_integer(alloc_resp.id);
        assert(v_id = 17) report "Unexpected ID";
        assert(to_integer(alloc_resp.address) = (16#2345600# + v_id * 1536)) report "Unexpected address";

        used(v_id, X"ABC");
        wait until io_irq = '1';

        io_read(io, 15, data);
        assert data(0) = '1' report "Expected an allocated block to be available";
        io_read(io, 8, data);
        assert data = X"11" report "Expected allocated block to have ID 11";
        io_read_word(io, 10, data16);
        assert data16 = X"0ABC" report "Expected length to be 0xABC";
        io_write(io, 15, X"00"); -- pop!
        io_read(io, 15, data);
        assert data(0) = '0' report "Expected no more allocated blocks to be available";


        free(0); --add free block 00

        alloc;
        v_id := to_integer(alloc_resp.id);
        assert(v_id = 0) report "Unexpected ID";
        assert(to_integer(alloc_resp.address) = (16#2345600# + v_id * 1536)) report "Unexpected address";

        alloc;
        assert(alloc_resp.error = '1') report "Allocation should now fail";

        -- check reset
        free(55); --add free block
        reset_queue;
        alloc;
        assert(alloc_resp.error = '1') report "Allocation should now fail";
        
        --for i in 0 to 130 loop -- deliberately overwrite as depth = 128
        for i in 0 to 126 loop
            free(i);
        end loop;

        alloc; -- 0
        alloc; -- 1
        alloc; -- 2
        alloc; -- 3
        alloc; -- 4

        v_id := to_integer(alloc_resp.id);
        assert(v_id = 4) report "Unexpected ID";
        
        for i in 0 to 4 loop
            free(i);
        end loop;

        alloc; -- 5
        alloc; -- 6
        alloc; -- 7
        alloc; -- 8
        alloc; -- 9

        v_id := to_integer(alloc_resp.id);
        assert(v_id = 9) report "Unexpected ID";

        wait;
    end process;
    
end architecture;
