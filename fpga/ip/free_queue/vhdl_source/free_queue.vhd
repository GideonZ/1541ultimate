-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : free_queue
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: A simple source of free memory blocks. Software provides, hardware uses.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.io_bus_pkg.all;
    use work.block_bus_pkg.all;
    
entity free_queue is
generic (
    g_store_size    : boolean := true;
    g_block_size    : natural := 1536 );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    -- free block interface
    alloc_req       : in  std_logic;
    alloc_resp      : out t_alloc_resp;

    used_req        : in  t_used_req;
    used_resp       : out std_logic;

    -- io interface for local cpu
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq          : out std_logic );

end entity;

architecture gideon of free_queue is
    function iif(c : boolean; t : natural; f : natural) return natural is
    begin
        if c then return t; else return f; end if;
    end function iif;
    
    constant c_mem_width    : natural := iif(g_store_size, 20, 8);

    type t_state is (idle, calc_offset, calc_addr, allocating, popping);
    signal state            : t_state;
    signal next_state       : t_state;

    signal alloc_resp_c     : t_alloc_resp;
    signal alloc_resp_i     : t_alloc_resp;
    signal offset_c         : unsigned(23 downto 0);
    signal offset_r         : unsigned(offset_c'range);
        
    signal alloc_head       : unsigned(7 downto 0);
    signal alloc_tail       : unsigned(7 downto 0);
    signal free_head        : unsigned(7 downto 0);
    signal free_tail        : unsigned(7 downto 0);
    signal alloc_head_next  : unsigned(7 downto 0);
    signal alloc_tail_next  : unsigned(7 downto 0);
    signal free_head_next   : unsigned(7 downto 0);
    signal free_tail_next   : unsigned(7 downto 0);

    signal table_addr       : unsigned(7 downto 0);
    signal table_rdata      : std_logic_vector(19 downto 0) := (others => '0');
    signal table_wdata      : std_logic_vector(19 downto 0) := (others => '0');
    signal table_en         : std_logic;
    signal table_we         : std_logic;

    signal sw_insert        : std_logic;
    signal sw_done          : std_logic;
    signal sw_addr          : std_logic_vector(25 downto 0) := (others => '0');
    signal sw_id            : std_logic_vector(7 downto 0) := (others => '0');
    signal sw_pop           : std_logic;
    signal sw_pop_valid     : std_logic;
    signal sw_pop_id        : std_logic_vector(7 downto 0) := (others => '0');
    signal sw_pop_size      : std_logic_vector(11 downto 0) := (others => '0');
    signal alloc_valid      : std_logic;
    signal soft_reset       : std_logic;
begin
    io_irq <= alloc_valid;
    
    process(clock)
        alias local_addr : unsigned(3 downto 0) is io_req.address(3 downto 0);
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;

            if sw_done = '1' then
                sw_insert <= '0';
                sw_pop <= '0';
            end if;
            
            -- fall through logic
            if alloc_valid = '0' then
                if sw_pop_valid = '1' then
                    sw_pop_id <= table_rdata(7 downto 0);
                    if g_store_size then
                        sw_pop_size <= table_rdata(19 downto 8);
                    end if;
                    alloc_valid <= '1';
                    sw_pop <= '0';
                end if;
                if (alloc_head /= alloc_tail) and sw_pop = '0' then
                    sw_pop <= '1';
                end if;
            end if;

            soft_reset <= '0';
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case local_addr is

                when X"0" =>
                    null;
                when X"1" =>
                    sw_addr(15 downto 8) <= io_req.data;
                when X"2" =>
                    sw_addr(23 downto 16) <= io_req.data;
                when X"3" => 
                    sw_addr(25 downto 24) <= io_req.data(1 downto 0);
                when X"4" => -- free / insert
                    sw_id <= io_req.data;
                    sw_insert <= '1';
                when X"F" =>
                    alloc_valid <= '0';
                when X"E" =>
                    soft_reset <= '1';
                when others =>
                    null; 
                end case;
            end if;

            if io_req.read = '1' then
                io_resp.ack <= '1';
                case local_addr is

                when X"4" =>
                    io_resp.data(0) <= sw_insert;
                when X"8" =>
                    io_resp.data <= sw_pop_id;
                when X"A" =>
                    io_resp.data <= sw_pop_size(7 downto 0);
                when X"B" =>
                    io_resp.data <= X"0" & sw_pop_size(11 downto 8);
                when X"F" =>
                    io_resp.data(0) <= alloc_valid;

                when others =>
                    null; 
                end case;
            end if;

            if reset = '1' then
                sw_insert <= '0';
                sw_pop <= '0';
                alloc_valid <= '0';
            end if;
        end if;
    end process;

    -- Important note:
    -- Because the allocation gets and used puts will always occur in the same order,
    -- the same memory can be used for both "fifos". The "free" can be in any order.
    -- So, there are four pointers pointing to the same array, while in normal fifo
    -- configurations, you would need two pointers per fifo, thus per memory area.
    -- As soon as the order of used/writes will become different from the order
    -- of allocation, the memory needs to be split into two areas. This can easily be
    -- achieved by appending a '0' in front of the address of the free pointers and
    -- a '1' in front of the alloc pointers (or vice versa). The number of elements
    -- then needs to be halved, or the memory doubled.

    i_table: entity work.spram
    generic map (
        g_width_bits => c_mem_width,
        g_depth_bits => 8,
        g_read_first => true
    )
    port map (
        clock        => clock,
        address      => table_addr,
        rdata        => table_rdata(c_mem_width-1 downto 0),
        wdata        => table_wdata(c_mem_width-1 downto 0),
        en           => table_en,
        we           => table_we
    );
    

    alloc_resp <= alloc_resp_i;
    
    process(alloc_req, alloc_resp_i, state, sw_insert, sw_pop, sw_id, sw_addr, used_req,
            free_head, free_tail, alloc_head, alloc_tail, offset_r, table_rdata)
    begin
        next_state <= state;
        alloc_resp_c <= alloc_resp_i;
        table_addr <= (others => 'X');
        table_wdata <= (others => 'X');
        table_we <= '0';
        table_en <= '0';
        free_head_next  <= free_head;
        free_tail_next  <= free_tail;
        alloc_head_next <= alloc_head;
        alloc_tail_next <= alloc_tail;
        used_resp <= '0';
        sw_done <= '0';
        sw_pop_valid <= '0';
        alloc_resp_c.done <= '0';

        if g_store_size then
            table_wdata(19 downto 8) <= std_logic_vector(used_req.bytes);
        end if;
        
        case state is
        when idle =>
            if sw_insert = '1' then
                table_addr  <= free_head;
                table_wdata(7 downto 0) <= sw_id;
                table_we <= '1';
                table_en <= '1';
                free_head_next <= free_head + 1;
                sw_done <= '1';
            elsif used_req.request = '1' then
                table_addr  <= alloc_head;
                table_wdata(7 downto 0) <= std_logic_vector(used_req.id);
                table_we <= '1';
                table_en <= '1';
                used_resp <= '1';
                alloc_head_next <= alloc_head + 1;
            elsif alloc_req = '1' and alloc_resp_i.done = '0' then
                table_addr <= free_tail;
                table_en <= '1';
                if free_tail = free_head then
                    alloc_resp_c.error <= '1';
                    alloc_resp_c.done <= '1';
                else
                    next_state <= allocating;
                end if;
            elsif sw_pop = '1' then
                table_addr  <= alloc_tail;
                table_en    <= '1';
                if alloc_tail /= alloc_head then
                    next_state <= popping;
                end if;
            end if;
            
        when allocating =>
            free_tail_next <= free_tail + 1;
            alloc_resp_c.id <= unsigned(table_rdata(7 downto 0));
            next_state <= calc_offset;
        
        when calc_offset => 
            offset_c <= to_unsigned(g_block_size, 16) * alloc_resp_i.id;
            next_state <= calc_addr;
        
        when calc_addr =>
            alloc_resp_c.address <= unsigned(sw_addr) + offset_r;
            alloc_resp_c.done <= '1';
            alloc_resp_c.error <= '0';
            next_state <= idle;
        
        when popping =>
            alloc_tail_next <= alloc_tail + 1;
            sw_pop_valid <= '1';
            next_state <= idle;
        end case;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            alloc_resp_i <= alloc_resp_c;
            offset_r <= offset_c;
            free_tail <= free_tail_next;
            free_head <= free_head_next;
            alloc_tail <= alloc_tail_next;
            alloc_head <= alloc_head_next;
            state <= next_state;

            if reset = '1' or soft_reset = '1' then
                state <= idle;
                alloc_resp_i <= c_alloc_resp_init;
                alloc_tail <= (others => '0');
                alloc_head <= (others => '0');
                free_tail <= (others => '0');
                free_head <= (others => '0');
            end if;
        end if;
    end process;

end architecture;

-- WAS:     free_queue  287 (110)   193 (71)    0 (0)   8704    3   0   0   0   100 0   94 (35) 36 (9)  157 (65)
-- ID:      free_queue  182 (181)   121 (121)   0 (0)   2048    1   0   0   0   101 0   61 (60) 24 (24) 97 (97) 
-- ID+SIZE: free_queue  196 (195)   133 (133)   0 (0)   5120    1   0   0   0   101 0   63 (62) 32 (32) 101 (101)   
-- size: only 14 cells more!
