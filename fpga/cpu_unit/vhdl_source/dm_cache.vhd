library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity dm_cache is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    client_req  : in  t_mem_req;
    client_resp : out t_mem_resp;
    
    mem_req     : out t_mem_burst_req;
    mem_resp    : in  t_mem_burst_resp );

end dm_cache;


architecture gideon of dm_cache is
    -- Our cache is 2K, and is one-set associative (direct mapped)
    -- This means that the lower 11 bits are taken from the CPU address
    -- while the upper address bits are matched against the tag ram.
    -- Cache line size is 4 bytes, and hence we need 2K/4 = 512 tag entries.
    -- Only the lower 32M is cachable, since I/O is above that range.

    constant c_address_width        : integer := client_req.address'length;
    constant c_data_width           : integer := client_req.data'length;
    constant c_cache_size_bits      : integer := 11;
    constant c_line_size_bits       : integer := 2;  -- 4 words per line (entry)
    constant c_tag_size_bits        : integer := c_cache_size_bits - c_line_size_bits;

    function index_of(a: unsigned(c_address_width-1 downto 0)) return unsigned is
    begin
        return a(c_cache_size_bits-1 downto 0);
    end function;
    
    signal any_request          : std_logic := '0';
    signal read_request         : std_logic := '0';
    signal read_request_d       : std_logic := '0';
    signal write_request        : std_logic := '0';
    signal write_request_d      : std_logic := '0';
    signal client_tag_d         : std_logic_vector(client_req.tag'range);
    signal ready                : std_logic := '0';
    signal read_la              : std_logic := '0';
    signal write_la             : std_logic := '0';
    
    signal tagram_request       : std_logic := '0';
    signal tagram_upd_addr      : unsigned(c_address_width-1 downto 0);
    signal allocate             : std_logic := '0';
    signal allocate_ff          : std_logic := '0';
    signal modify               : std_logic := '0';
    signal modify_ff            : std_logic := '0';
    signal invalidate           : std_logic := '0';
    signal error                : std_logic := '0';
    signal line_dirty           : std_logic := '0';
    signal line_valid           : std_logic := '0';
    signal cache_miss           : std_logic := '0';
    signal cache_hit            : std_logic := '0';
    signal old_address          : unsigned(c_address_width-1 downto 0);
    signal old_addr_la          : unsigned(c_address_width-1 downto 0) := (others => '1');
    signal address_la           : unsigned(c_address_width-1 downto 0);

    signal fill_index           : unsigned(c_cache_size_bits-1 downto 0) := (others => '0');
    signal fill_valid           : std_logic;
    signal fill_data            : std_logic_vector(c_data_width-1 downto 0);

    signal write_bus_address    : unsigned(c_address_width-1 downto 0);
    signal write_bus_write      : std_logic;
    signal write_bus_write_d    : std_logic;
    signal write_bus_data       : std_logic_vector(c_data_width-1 downto 0);
    signal write_reg_invalidate : std_logic;
    
    signal reg_address          : unsigned(c_address_width-1 downto 0);
    signal reg_data             : std_logic_vector(c_data_width-1 downto 0);
    signal reg_hit              : std_logic;
    signal reg_valid            : std_logic;
    signal store_reg            : std_logic := '0';
    signal cache_index          : unsigned(c_cache_size_bits-1 downto 0);
    signal cache_wdata          : std_logic_vector(c_data_width-1 downto 0);
    signal cache_rdata          : std_logic_vector(c_data_width-1 downto 0);
    signal cache_we             : std_logic;
    
    signal mem_busy             : std_logic := '0';
    signal need_mem_access      : std_logic := '0';
    signal mem_req_i            : std_logic;
    signal mem_rwn              : std_logic;
    signal mem_address          : unsigned(c_address_width-1 downto 0);

    signal dirty_d              : std_logic;
    signal burst_count          : unsigned(c_line_size_bits-1 downto 0);
    signal burst_count_d        : unsigned(c_line_size_bits-1 downto 0);

    signal mem_wrfifo_put       : std_logic;
    signal mem_rdfifo_get       : std_logic;
    
    signal dirty_by_register    : std_logic;
    signal data_to_dram_mux     : std_logic;
    signal trigger_reg_to_dram  : std_logic;
    
    type t_state is (idle, check_dirty, fill, deferred);
    signal state                : t_state;
begin
    any_request     <= client_req.request and ready;
    read_request    <= client_req.request and client_req.read_writen and ready;
    write_request   <= client_req.request and not client_req.read_writen and ready;
    ready           <= '1' when mem_busy='0' and need_mem_access='0' else '0';
    need_mem_access <= cache_miss;

    process(clock)
    begin
        if rising_edge(clock) then
            read_request_d  <= read_request;
            write_request_d <= write_request;
            client_tag_d    <= client_req.tag;
            if ready='1' then
                address_la <= client_req.address;
                read_la    <= client_req.request and client_req.read_writen;
                write_la   <= client_req.request and not client_req.read_writen;
            end if;
        end if;
    end process;
    
    tagram_request     <= client_req.request and ready;
    tagram_upd_addr    <= address_la when allocate='1' else
                          reg_address;                          

    i_tag_ram: entity work.tag_ram 
    generic map (
        g_address_width     => c_address_width,
        g_line_size_bits    => c_line_size_bits,
        g_index_bits        => c_tag_size_bits )
    port map (
        clock       => clock,
        reset       => reset,

        query_addr  => client_req.address,
        do_query    => tagram_request,

        update_addr => tagram_upd_addr,
        allocate    => allocate,
        modify      => modify,
        invalidate  => invalidate,

        error       => error,
        dirty       => line_dirty,
        valid       => line_valid,
        miss        => cache_miss,
        hit         => cache_hit,
        address_out => old_address );

    write_bus_write      <= client_req.request and not client_req.read_writen and ready;
    write_bus_address    <= client_req.address;
    write_bus_data       <= client_req.data;
    write_reg_invalidate <= store_reg or (trigger_reg_to_dram and dirty_d);
    
    i_last_write: entity work.delayed_write
    generic map (
        g_address_width     => c_address_width,
        g_data_width        => c_data_width )
    port map (
        clock       => clock,
        reset       => reset,

        match_addr  => client_req.address,
        match_req   => read_request,
        
        bus_address => write_bus_address,
        bus_write   => write_bus_write,
        bus_wdata   => write_bus_data,
        invalidate  => write_reg_invalidate,
        
        reg_address => reg_address,
        reg_out     => reg_data,
        reg_hit     => reg_hit,
        reg_valid   => reg_valid );

    cache_index <= fill_index when (state /= idle) else
                   index_of(reg_address) when store_reg='1' else
                   index_of(client_req.address);
    cache_wdata <= fill_data when fill_valid='1' else
                   reg_data;
    cache_we    <= fill_valid or store_reg;


    -- we store and invalidate the register in the following occasions:
    -- 1) There is another write, and the register is valid. (register replace with new value/address, and will set to valid again.)
    -- 2) There is a cache miss, and the register got not just written the cycle before. Because, if that is the case
    --    the cache miss was caused by the write that preceded the cycle, and hence the register cannot yet be stored,
    --    since the cache line needs to be filled first. We'll need to wait for the next opportunity.
    
    -- Another option is to accept that a cacheline is written back from the cache ram OR the register (place
    -- a multiplexer in the data to dram). This has the advantage that the cache_index multiplexer does not
    -- depend on cache_miss, which is a long path. In order to do this, we need to compare the register
    -- address with the old address. We can do this in a registered way; since the reading of the cache ram
    -- also has a latency of 1.
    
    store_reg   <= '1' when (reg_valid='1' and write_bus_write='1') else 
--                   '1' when (reg_valid='1' and cache_miss='1' and write_bus_write_d='0') else
                   '0';

    i_cache_ram: entity work.spram
    generic map (
        g_width_bits  => c_data_width,
        g_depth_bits  => c_cache_size_bits,
        g_read_first  => false,
        g_storage     => "auto" )
    port map (
        clock         => clock,
        address       => cache_index,
        rdata         => cache_rdata,
        wdata         => cache_wdata,
        en            => '1',
        we            => cache_we );

    -- read data multiplexer
    fill_valid       <= mem_resp.rdata_av;
    fill_data        <= mem_resp.data;
    client_resp.rack <= client_req.request and ready;
    
    process(reg_hit, reg_data, read_request_d, client_tag_d, cache_hit, cache_rdata, fill_data, fill_valid, burst_count, read_la, address_la)
    begin
        client_resp.dack_tag <= (others => '0');
        if reg_hit='1' then -- latest written data goes first!
            client_resp.data <= reg_data;
            if read_request_d='1' then
                client_resp.dack_tag <= client_tag_d;
            end if;
        elsif cache_hit='1' then
            client_resp.data <= cache_rdata;
            if read_request_d='1' then
                client_resp.dack_tag <= client_tag_d;
            end if;
        else
            client_resp.data <= fill_data;
            -- TODO: generate dack when correct word passes by (not necessary, but will increase performance)
            if fill_valid='1' and burst_count = address_la(burst_count'range) and read_la='1' then
                client_resp.dack_tag <= client_tag_d;
            end if;
        end if;
    end process;
    
    allocate <= allocate_ff;
    modify   <= store_reg or modify_ff;
        
     -- cache_miss='1' =>
        -- if dirty and not g_write_through =>
            -- issue mem write request on cache old address.
            -- wait until memory transaction has finished. data goes out from cache ram (cache data)
        -- if write_request_d='1' and g_allocate_on_write =>
             -- allocate on new address (write register address!)
        -- elsif read_request_d='1' =>
            -- allocate on new address
            -- issue mem read request on new address. data is written in cache.            

    -- allocate also means that we are issuing a memory read, in order to fill the cacheline.
    -- however, when an allocate happens because of a write; then we know that the register
    -- holds newer data than the fill has just written into the cache. No problem, because
    -- the same is true for a delayed write. Any subsequent read that matches the last write
    -- register will still match and give a hit. 
    
    -- Special care should be taken when writing back data to ram, while the register still
    -- holds data that belongs to the cache line that is going to be written. In order to avoid
    -- this scenario, the last write register should be written first in the cacheline. Since we
    -- have a direct mapped cache, we can easily do this, because we know that the register data
    -- *always* holds data that belongs to an active cache line. This is enforced by allocate on write.

    p_cache_control: process(clock)
    begin
        if rising_edge(clock) then
            write_bus_write_d <= write_bus_write;
            burst_count_d     <= burst_count;

            allocate_ff <= '0';
            modify_ff   <= '0';
            mem_req_i   <= '0';
            
            case state is
            when idle =>
                if cache_miss='1' then
                    if mem_resp.ready='1' then
                        -- issue read access (priority read over write)
                        mem_req_i   <= '1';
                        mem_rwn     <= '1';
                        mem_address <= address_la;
                        state <= check_dirty;
                    else
                        state <= deferred;
                    end if;                  
                end if;
                dirty_d <= (dirty_by_register or line_dirty) and cache_miss; -- dirty will be our read enable from cache :)
                trigger_reg_to_dram <= dirty_by_register;
                
            when check_dirty => -- sequences through 'line_size' words
                mem_address <= old_address;
                mem_rwn     <= '0'; -- write
                if dirty_d='0' then
                    state <= fill;
                else -- dirty_d='1'                
                    burst_count <= burst_count + 1;
                    if signed(burst_count) = -1 then -- last?
                        mem_req_i <= '1'; -- issue the write request to memctrl
                        dirty_d <= '0';
                        state <= fill;
                    end if;
                end if;

            when fill =>
                if mem_resp.rdata_av='1' then
                    burst_count <= burst_count + 1;
                    if signed(burst_count) = -2 then -- doesn't work for burstlen=2!
                        allocate_ff <= '1'; -- update tag
                        modify_ff <= write_la;
                    end if;
                    if signed(burst_count) = -1 then -- last?
                        state <= idle;
                    end if;
                end if;
                -- asynchronously: mem_rdfifo_get <= '1' when state = fill and mem_resp.rdata_av='1'.

            when others =>
                null;
            end case;

            mem_wrfifo_put <= dirty_d; -- latency of blockram
                        
            if reset='1' then
                burst_count <= (others => '0');
                dirty_d     <= '0';
                state       <= idle;
                mem_rwn     <= '1';
                mem_req_i   <= '0';
            end if;
        end if;
    end process;

    mem_rdfifo_get <= '1' when state = fill and mem_resp.rdata_av='1' else '0';
    
    -- index to the cache for back-office operations (line in, line out)
    fill_index <= address_la(cache_index'high downto c_line_size_bits) & burst_count;
    
    dirty_by_register <= '1' when reg_address(c_cache_size_bits-1 downto c_line_size_bits) =
                                  old_address(c_cache_size_bits-1 downto c_line_size_bits) and
                            reg_valid='1' and write_bus_write_d='0' else '0';

    data_to_dram_mux  <= '1' when trigger_reg_to_dram='1' and (burst_count_d=reg_address(c_line_size_bits-1 downto 0)) else
                         '0';
    
    mem_busy <= '1' when (state/= idle) else '0';

    mem_req.request     <= mem_req_i;
    mem_req.read_writen <= mem_rwn;
    mem_req.address     <= mem_address(mem_address'high downto c_line_size_bits) & to_unsigned(0, c_line_size_bits);
    mem_req.data        <= cache_rdata when data_to_dram_mux='0' else reg_data;
    mem_req.data_pop    <= '1' when (state = fill) and mem_resp.rdata_av='1' else '0';
    mem_req.data_push   <= mem_wrfifo_put;
    
--  i_cache_control: entity work.cache_controller
--  port map (
--      clock           => clock,
--      reset           => reset,
--
--      allocate        => allocate,
--      modify          => modify,
--      invalidate      => invalidate,
--                      
--      dirty           => dirty,
--      miss            => cache_miss,
--      hit             => cache_hit,
--
--      
--
--      
--      mem_req         => mem_req,
--      mem_resp        => mem_resp );

end gideon;
