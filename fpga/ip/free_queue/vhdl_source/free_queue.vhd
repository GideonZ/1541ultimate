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
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    -- free block interface
    block_req       : in  t_block_req;
    block_resp      : out t_block_resp;

    -- io interface for local cpu
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq          : out std_logic );

end entity;

architecture gideon of free_queue is
    signal block_resp_c : t_block_resp;
    signal block_resp_i : t_block_resp;

    type t_state is (idle, lookup_alloc, lookup_write, wait_fifo, wait_fifo_sw);
    signal state            : t_state;
    signal next_state       : t_state;
    
    signal alloc_pop        : std_logic := '0';
    signal alloc_push       : std_logic;
    signal alloc_ready      : std_logic := '1';
    signal alloc_valid      : std_logic;
    signal alloc_full       : std_logic;
    signal alloc_wdata      : std_logic_vector(7 downto 0);
    signal alloc_rdata      : std_logic_vector(7 downto 0);
    
    signal free_push        : std_logic := '0';
    signal free_pop         : std_logic;
    signal free_ready       : std_logic := '1';
    signal free_valid       : std_logic;
    signal free_full        : std_logic;
    signal free_wdata       : std_logic_vector(7 downto 0);
    signal free_rdata       : std_logic_vector(7 downto 0);
    
    signal lookup_address   : unsigned(7 downto 0);
    signal lookup_wdata     : std_logic_vector(31 downto 0);
    signal lookup_rdata     : std_logic_vector(31 downto 0);
    signal lookup_en        : std_logic;
    signal lookup_we        : std_logic;

    signal sw_insert        : std_logic;
    signal sw_addr_valid    : std_logic;
    signal sw_done          : std_logic;
    signal sw_addr          : std_logic_vector(25 downto 8) := (others => '0');
    signal sw_id            : unsigned(7 downto 0) := (others => '0');
begin
    io_irq <= alloc_valid;
    
    process(clock)
        alias local_addr : unsigned(3 downto 0) is io_req.address(3 downto 0);
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;
            alloc_pop <= '0';

            if sw_done = '1' then
                sw_insert <= '0';
            end if;
            
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
                when X"4" => -- free
                    sw_id <= unsigned(io_req.data);
                    sw_insert <= '1';
                    sw_addr_valid <= '0';                                
                when X"5" => -- insert
                    sw_id <= unsigned(io_req.data);
                    sw_insert <= '1';
                    sw_addr_valid <= '1';                                
                when X"F" =>
                    alloc_pop <= '1';
                    
                when others =>
                    null; 
                end case;
            end if;

            if io_req.read = '1' then
                io_resp.ack <= '1';
                case local_addr is

                when X"4"|X"5" =>
                    io_resp.data(0) <= sw_insert;
                when X"6" =>
                    io_resp.data(0) <= free_full;
                when X"8" =>
                    io_resp.data <= alloc_rdata;
                when X"F" =>
                    io_resp.data(0) <= alloc_valid;

                when others =>
                    null; 
                end case;
            end if;

            if reset = '1' then
                sw_insert <= '0';
                sw_addr_valid <= '0';
            end if;
        end if;
    end process;

    i_lookup: entity work.spram
    generic map (
        g_width_bits => 32,
        g_depth_bits => 8,
        g_read_first => true
    )
    port map (
        clock        => clock,
        address      => lookup_address,
        rdata        => lookup_rdata,
        wdata        => lookup_wdata,
        en           => lookup_en,
        we           => lookup_we
    );
    
    block_resp <= block_resp_i;

    process (block_req, block_resp_i, state, lookup_rdata, alloc_ready, alloc_full, free_ready, free_rdata, free_valid,
             sw_insert, sw_id, sw_addr, sw_addr_valid)
    begin
        next_state <= state;

        block_resp_c <= block_resp_i;
        block_resp_c.done <= '0';

        lookup_address <= (others => 'X');
        lookup_en <= '0';
        lookup_we <= '0';
        lookup_wdata <= (others => 'X');

        free_pop  <= '0';
        free_push <= '0';
        sw_done   <= '0';
        free_wdata <= (others => 'X');
        
        alloc_push <= '0';
        alloc_wdata <= (others => 'X');
              
        case state is
        when idle =>
            if block_req.request = '1' and block_resp_i.done = '0' then
                block_resp_c.error <= '0';
                
                case block_req.command is
                when allocate => -- take element from ID free fifo, lookup params and return
                    lookup_address <= unsigned(free_rdata);
                    lookup_en <= '1';

                    if free_valid = '1' then
                        next_state <= lookup_alloc;
                    else
                        block_resp_c.done <= '1';
                        block_resp_c.error <= '1';
                    end if;
                
                when write => -- read params for RMW cycle                         
                    next_state     <= lookup_write;
                    lookup_address <= block_req.id;
                    lookup_en      <= '1';

                when others => -- not yet implemented
                    block_resp_c.error <= '1';
                    block_resp_c.done <= '1';
                end case;

            elsif sw_insert = '1' then
                lookup_address <= sw_id;
                lookup_wdata(17 downto 0)  <= std_logic_vector(sw_addr(25 downto 8));
                lookup_wdata(29 downto 18) <= (others => '0');
                lookup_wdata(31 downto 30) <= "00";
                lookup_en <= sw_addr_valid;
                lookup_we <= '1';

                free_wdata <= std_logic_vector(sw_id);
                free_push  <= '1';
                next_state <= wait_fifo_sw;
            end if;                                        

        when lookup_alloc =>
            block_resp_c.address <= unsigned(lookup_rdata(17 downto 0)) & X"00";
            block_resp_c.id      <= unsigned(free_rdata);
            free_pop             <= '1';
            next_state <= wait_fifo;
        
        when lookup_write =>
            lookup_address <= block_req.id;
            lookup_wdata(17 downto 0)  <= lookup_rdata(17 downto 0);
            lookup_wdata(29 downto 18) <= std_logic_vector(block_req.bytes);
            lookup_wdata(31 downto 30) <= "00";
            lookup_en <= '1';
            lookup_we <= '1';

            if alloc_full = '1' then
                block_resp_c.error <= '1';
                block_resp_c.done <= '1';
            else
                alloc_wdata <= std_logic_vector(block_req.id);
                alloc_push <= '1';
                next_state <= wait_fifo; 
            end if;

        when wait_fifo =>
            if alloc_ready = '1' and free_ready = '1' then
                block_resp_c.done <= '1';
                next_state <= idle; 
            end if;

        when wait_fifo_sw =>
            if free_ready = '1' then
                sw_done <= '1';
                next_state <= idle; 
            end if;

        end case;            
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            block_resp_i <= block_resp_c;
            state <= next_state;
            if reset = '1' then
                state <= idle;
                block_resp_i <= c_block_resp_init;
            end if;
        end if;
    end process;

    i_free_fifo: entity work.sync_fifo
    generic map (
        g_depth        => 256,
        g_data_width   => 8,
        g_threshold    => 13,
        g_fall_through => true
    )
    port map (
        clock          => clock,
        reset          => reset,
        rd_en          => free_pop,
        wr_en          => free_push,
        din            => free_wdata,
        dout           => free_rdata,
        flush          => '0',
        full           => free_full,
        valid          => free_valid
    );
    
    i_alloc_fifo: entity work.sync_fifo
    generic map (
        g_depth        => 256,
        g_data_width   => 8,
        g_threshold    => 13,
        g_fall_through => true
    )
    port map (
        clock          => clock,
        reset          => reset,
        rd_en          => alloc_pop,
        wr_en          => alloc_push,
        din            => alloc_wdata,
        dout           => alloc_rdata,
        flush          => '0',
        full           => alloc_full,
        valid          => alloc_valid
    );

end architecture;
