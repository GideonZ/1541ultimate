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
    mem_resp    : in  t_mem_burst_resp;
    
    hit_count   : out unsigned(31 downto 0);
    miss_count  : out unsigned(31 downto 0) );

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

	constant c_tag_width	        : natural := 2 + c_address_width - c_cache_size_bits; 

    function cache_index_of(a: unsigned(c_address_width-1 downto 0)) return unsigned is
    begin
        return a(c_cache_size_bits-1 downto 0);
    end function;
    
    function tag_index_of(a: unsigned(c_address_width-1 downto 0)) return unsigned is
    begin
        return a(c_cache_size_bits-1 downto c_line_size_bits);
    end function;

    function get_addr_high(a: unsigned(c_address_width-1 downto 0)) return unsigned is
    begin
        return a(c_address_width-1 downto c_cache_size_bits);
    end function;

    type t_tag is record
        address_high    : unsigned(c_address_width-1 downto c_cache_size_bits);
        dirty           : std_logic;
        valid           : std_logic;
    end record;
    
    function tag_pack(t : t_tag) return std_logic_vector is
        variable ret : std_logic_vector(c_tag_width-1 downto 0);
    begin
        ret := t.dirty & t.valid & std_logic_vector(t.address_high);
        return ret;
    end function;

    function tag_unpack(v : std_logic_vector(c_tag_width-1 downto 0)) return t_tag is
        variable ret : t_tag;
    begin
        ret.dirty := v(v'high);
        ret.valid := v(v'high-1);
        ret.address_high := unsigned(v(v'high-2 downto 0));
        return ret;
    end function;

    signal any_request          : std_logic := '0';
    signal read_request         : std_logic := '0';
    signal read_request_d       : std_logic := '0';
    signal write_request        : std_logic := '0';
    signal write_request_d      : std_logic := '0';
    signal ready                : std_logic := '0';
    signal read_la              : std_logic := '0';
    signal write_la             : std_logic := '0';
    signal tag_la               : std_logic_vector(client_req.tag'range);
    signal do_query_d           : std_logic;
    
    signal rd_address           : unsigned(c_address_width-1 downto 0);
    signal wr_address           : unsigned(c_address_width-1 downto 0);

    signal cache_rd_index       : unsigned(c_cache_size_bits-1 downto 0);
    signal cache_wr_index       : unsigned(c_cache_size_bits-1 downto 0);
    signal cache_wdata          : std_logic_vector(c_data_width-1 downto 0);
    signal cache_data_out       : std_logic_vector(c_data_width-1 downto 0);
    signal cache_rdata          : std_logic_vector(c_data_width-1 downto 0);
    signal cache_we             : std_logic;
    signal cache_b_en           : std_logic;
    signal cache_rd_en          : std_logic;
    signal tag_rd_index         : unsigned(c_cache_size_bits-1 downto c_line_size_bits);
    signal tag_wr_index         : unsigned(c_cache_size_bits-1 downto c_line_size_bits);

    signal tag_wdata            : std_logic_vector(c_tag_width-1 downto 0);
    signal tag_rdata            : std_logic_vector(c_tag_width-1 downto 0);
    
    signal rd_tag               : t_tag;
    signal wr_tag               : t_tag;
    signal fill_tag             : t_tag;
    signal last_write_tag       : t_tag;

    signal hit_i                : std_logic := '0';
    signal cache_miss           : std_logic := '0';
    signal cache_hit            : std_logic := '0';
    signal old_address          : unsigned(c_address_width-1 downto 0);
    signal address_la           : unsigned(c_address_width-1 downto 0);

    -- back office
    signal fill_high            : unsigned(c_address_width-1 downto c_line_size_bits) := (others => '0');
    signal fill_address         : unsigned(c_address_width-1 downto 0) := (others => '0');
    signal fill_valid           : std_logic;
    signal fill_data            : std_logic_vector(c_data_width-1 downto 0);
    signal burst_count          : unsigned(c_line_size_bits-1 downto 0);
    signal burst_count_d        : unsigned(c_line_size_bits-1 downto 0);

    type t_state is (idle, check_dirty, fill, deferred);
    signal state                : t_state;
    signal dirty_d              : std_logic;

    -- signals related to delayed write register
    signal last_write_address   : unsigned(c_address_width-1 downto 0);
    signal last_write_data      : std_logic_vector(c_data_width-1 downto 0);
    signal last_write_hit       : std_logic;
    signal last_write_valid     : std_logic;
    signal store_reg            : std_logic := '0';
    signal store_after_fill     : std_logic := '0';
    
    -- memory interface
    signal mem_busy             : std_logic := '0';
    signal need_mem_access      : std_logic := '0';
    signal mem_req_i            : std_logic;
    signal mem_rwn              : std_logic;
    signal mem_address          : unsigned(c_address_width-1 downto 0);
    signal mem_wdata            : std_logic_vector(c_data_width-1 downto 0);

    signal mem_wrfifo_put       : std_logic;
    signal mem_rdfifo_get       : std_logic;
    
    signal helper_data_to_ram   : std_logic_vector(c_data_width-1 downto 0);
    signal helper_data_from_ram : std_logic_vector(c_data_width-1 downto 0);
    
    -- statistics
    signal hit_count_i          : unsigned(31 downto 0) := (others => '0');
    signal miss_count_i         : unsigned(31 downto 0) := (others => '0');
    
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
            do_query_d      <= '0';
            if ready='1' then
                do_query_d    <= client_req.request;
                tag_la        <= client_req.tag;
                address_la    <= client_req.address;
                read_la       <= client_req.request and client_req.read_writen;
                write_la      <= client_req.request and not client_req.read_writen;
            end if;
        end if;
    end process;
    
    -- main address multiplexer
    rd_address         <= client_req.address;
    wr_address         <= fill_address when (fill_valid='1' or dirty_d='1') else
                          last_write_address;
                          
    cache_rd_index <= cache_index_of(rd_address);
    cache_wr_index <= cache_index_of(wr_address);
   
    cache_wdata    <= fill_data when fill_valid='1' else
                      last_write_data;
    wr_tag         <= fill_tag when fill_valid='1' else
                      last_write_tag;

    cache_we       <= fill_valid or store_reg;
    cache_b_en     <= cache_we or dirty_d; -- dirty_d is set during fill operation and causes read enable here
    cache_rd_en    <= client_req.request and ready;

    fill_tag.address_high <= get_addr_high(fill_address);
    fill_tag.dirty        <= '0';
    fill_tag.valid        <= '1';
    
    last_write_tag.address_high <= get_addr_high(last_write_address);
    last_write_tag.dirty  <= '1';
    last_write_tag.valid  <= last_write_valid;
    

    i_cache_ram: entity work.dpram_rdw
    generic map (
        g_width_bits   => c_data_width,
        g_depth_bits   => c_cache_size_bits,
        g_storage      => "auto" )
    port map (
        clock         => clock,

        a_address     => cache_rd_index,
        a_rdata       => cache_rdata,
        a_en          => cache_rd_en,
        
        b_address     => cache_wr_index,
        b_rdata       => cache_data_out,
        b_wdata       => cache_wdata,
        b_en          => cache_b_en,
        b_we          => cache_we );


    tag_rd_index       <= tag_index_of(rd_address);    
    tag_wr_index       <= tag_index_of(wr_address);
    rd_tag             <= tag_unpack(tag_rdata);
    tag_wdata          <= tag_pack(wr_tag);

    i_tag_ram: entity work.dpram_rdw 
    generic map (
        g_width_bits  => c_tag_width,
        g_depth_bits  => c_tag_size_bits )
    port map (
        clock       => clock,
        
        a_address   => tag_rd_index,
        a_rdata     => tag_rdata,
        a_en        => cache_rd_en,
        
        b_address   => tag_wr_index,
        b_wdata     => tag_wdata,
        b_en        => cache_we,
        b_we        => cache_we );

	hit_i        <= '1' when rd_tag.valid='1' and (rd_tag.address_high = get_addr_high(address_la)) else '0';
	cache_hit    <= hit_i and do_query_d;
	cache_miss   <= not hit_i and do_query_d;
    old_address  <= rd_tag.address_high & address_la(c_cache_size_bits-1 downto 0); -- recombine

    -- handle writes
    process(clock)
    begin
        if rising_edge(clock) then
            last_write_hit <= '0';
            if client_req.request='1' and client_req.read_writen='1' and ready='1' then -- hit only occurs on reads
                if (last_write_address = client_req.address) and last_write_valid='1' then -- address equal and valid?
                    last_write_hit <= '1';
                end if;
            end if;

            if client_req.request='1' and client_req.read_writen='0' and ready='1' then
                last_write_data    <= client_req.data;
                last_write_address <= client_req.address;
                last_write_valid <= '1';
            elsif store_reg='1' then
                last_write_valid <= '0';
            end if;                
        end if;
    end process;

    store_reg   <= '1' when (last_write_valid='1' and (cache_hit='1' or store_after_fill='1')) else 
                   '0';

    -- end handle writes

    -- read data multiplexer
    fill_valid           <= mem_resp.rdata_av;
    fill_data            <= mem_resp.data;
    client_resp.rack     <= client_req.request and ready;
    client_resp.rack_tag <= client_req.tag when client_req.request='1' and ready='1' else (others => '0');
    
    process(last_write_hit, last_write_data, read_request_d, tag_la, cache_hit, cache_rdata,
            fill_data, fill_valid, burst_count, read_la, address_la)
    begin
        client_resp.dack_tag <= (others => '0');
--        if last_write_hit='1' then -- latest written data goes first! -- solving RAW hazzard
--            client_resp.data <= last_write_data;
--            if read_request_d='1' then
--                client_resp.dack_tag <= tag_la;
--            end if;
--        elsif cache_hit='1' then
        if cache_hit='1' then
            client_resp.data <= cache_rdata;
            if read_request_d='1' then
                client_resp.dack_tag <= tag_la;
            end if;
        else
            client_resp.data <= fill_data;
            -- Generate dack when correct word passes by (not necessary, but will increase performance)
            if fill_valid='1' and burst_count = address_la(burst_count'range) and read_la='1' then
                client_resp.dack_tag <= tag_la;
            end if;
        end if;
    end process;
    -- end read data multiplexer
    
    p_cache_control: process(clock)
    begin
        if rising_edge(clock) then
            burst_count_d     <= burst_count;
            store_after_fill  <= '0';
            mem_req_i   <= '0';
            
            if cache_miss='1' then
                miss_count_i <= miss_count_i + 1;
            end if;
            if cache_hit='1' then
                hit_count_i <= hit_count_i + 1;
            end if;

            case state is
            when idle =>
                -- There are a few scenarios that could cause a miss:
                -- Read miss:  last_write_register is not valid, because it should already have been written in the cache!
                -- Write miss: last_write_register is always valid, since it was just set. In this scenario the last write register
                -- holds data that still needs to be written to the cache, BUT couldn't do it because of the miss. The data in the cache
                -- that is flushed to DRAM is never the data in the register, otherwise it would have been a hit. The fill cycle that
                -- follows will check dirty, do a write out of the dirty data from cache, and then fills the cacheline with data from
                -- the DRAM, and then will issue the command to store the register. Obviously this immediately sets the line to dirty.
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
                dirty_d <= rd_tag.dirty and cache_miss; -- dirty will be our read enable from cache :)
                --fill_high <= old_address(old_address'high downto c_line_size_bits); -- high bits don't matter here (this is correct!)
                --fill_high <= address_la(old_address'high downto c_line_size_bits); -- high bits don't matter here (optimization!!)
                
            when deferred =>
                if mem_resp.ready='1' then
                    -- issue read access (priority read over write)
                    mem_req_i   <= '1';
                    mem_rwn     <= '1';
                    mem_address <= address_la;
                    state <= check_dirty;
                end if;

            when check_dirty => -- sequences through 'line_size' words
                mem_address <= old_address;
                mem_rwn     <= '0'; -- write
                if dirty_d='0' then
                    --fill_high <= address_la(address_la'high downto c_line_size_bits); -- high bits do matter here
                    state <= fill;
                else -- dirty_d='1'                
                    burst_count <= burst_count + 1;
                    if signed(burst_count) = -1 then -- last?
                        mem_req_i <= '1'; -- issue the write request to memctrl
                        dirty_d <= '0';
                        --fill_high <= address_la(address_la'high downto c_line_size_bits); -- high bits do matter here
                        state <= fill;
                    end if;
                end if;

            when fill =>
                if mem_resp.rdata_av='1' then
                    burst_count <= burst_count + 1;
                    if signed(burst_count) = -1 then -- last?
                        state <= idle;
                        store_after_fill <= last_write_valid; -- this will occur during idle
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
    fill_high    <= address_la(old_address'high downto c_line_size_bits);
    fill_address <= fill_high & burst_count;

    mem_busy <= '1' when (state/= idle) else '0';

    mem_wdata           <= cache_data_out;
    mem_req.request     <= mem_req_i;
    mem_req.read_writen <= mem_rwn;
    mem_req.address     <= mem_address(mem_address'high downto c_line_size_bits) & to_unsigned(0, c_line_size_bits);
    mem_req.data        <= mem_wdata;
    mem_req.data_pop    <= mem_rdfifo_get;
    mem_req.data_push   <= mem_wrfifo_put;
    
    helper_data_to_ram   <= mem_wdata     when mem_wrfifo_put='1' else (others => 'Z');
    helper_data_from_ram <= mem_resp.data when mem_rdfifo_get='1' else (others => 'Z');

    hit_count           <= hit_count_i;
    miss_count          <= miss_count_i;

end gideon;
