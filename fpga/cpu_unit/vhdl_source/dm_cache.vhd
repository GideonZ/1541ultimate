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
    
    mem_req     : out t_mem_req;
    mem_resp    : in  t_mem_resp );

end dm_cache;


architecture gideon of dm_cache is
    -- Our cache is 2K, and is one-set associative (direct mapped)
    -- This means that the lower 11 bits are taken from the CPU address
    -- while the upper address bits are matched against the tag ram.
    -- Cache line size is 4 bytes, and hence we need 2K/4 = 512 tag entries.
    -- Only the lower 32M is cachable, since I/O is above that range.

    constant c_cache_size_bits      : integer := 11;
    constant c_line_size_bits       : integer := 2;
    constant c_tag_size_bits        : integer := c_cache_size_bits - c_line_size_bits;
    constant c_tag_address_width    : integer := client_req.address'length - c_cache_size_bits; -- 26-11=15
    constant c_total_tag_width      : integer := c_tag_address_width + 2; -- the valid bit and the dirty bit
    constant c_data_dirty_bits      : integer := 1; --2 ** c_line_size_bits;
    constant c_total_data_width     : integer := c_data_dirty_bits + client_req.data'length;
    
    type t_state is (idle, reading, writing);

    signal tag_address              : unsigned(c_tag_size_bits-1 downto 0);
    signal data_address             : unsigned(c_cache_size_bits-1 downto 0);
    signal wdata                    : std_logic_vector(c_total_data_width-1 downto 0);
    signal rdata                    : std_logic_vector(c_total_data_width-1 downto 0);
    signal wtag                     : std_logic_vector(c_total_tag_width-1 downto 0);
    signal wtag_d                   : std_logic_vector(c_total_tag_width-1 downto 0);
    signal rtag                     : std_logic_vector(c_total_tag_width-1 downto 0);
    signal hit                      : std_logic;
    signal tag_valid                : std_logic;
    signal tag_dirty                : std_logic;
    signal tag_en                   : std_logic;
    signal tag_we                   : std_logic;
    signal data_en                  : std_logic;
    signal data_we                  : std_logic;
    signal write_d                  : std_logic;
    signal read_d                   : std_logic;
    signal do_write                 : std_logic;
    signal do_read                  : std_logic;
    signal busy                     : std_logic;
    signal rack                     : std_logic;
    signal dack                     : std_logic;
    signal last_tag                 : std_logic_vector(client_req.tag'range);
    signal mem_busy                 : std_logic := '0';
    signal mem_req_ff               : std_logic;
    signal mem_state                : t_state;
begin
    tag_address  <= client_req.address(c_cache_size_bits-1 downto c_line_size_bits);
    data_address <= client_req.address(c_cache_size_bits-1 downto 0);
    wdata <= '1' & client_req.data; -- dirty bit
    wtag  <= '1' & -- valid bit
             not client_req.read_writen & -- dirty bit
             std_logic_vector(client_req.address(client_req.address'high downto c_cache_size_bits)); -- address msbs

    process(clock)
    begin
        if rising_edge(clock) then
            wtag_d   <= wtag;
            write_d  <= client_req.request and not client_req.read_writen;
            read_d   <= client_req.request and client_req.read_writen;
            last_tag <= client_req.tag;

            case mem_state is
            when idle =>
                if do_read='1' then
                    mem_req_ff <= '1';
                    mem_state <= reading;
                elsif do_write='1' then
                    mem_req_ff <= '1';
                    mem_state <= writing;
                end if;
            when reading =>
                if mem_resp.rack='1' then
                    mem_req_ff <= '0';
                end if;
                if mem_resp.last='1' then
                    mem_state <= idle;
                end if;
            when writing =>
                if mem_resp.rack='1' then
                    mem_req_ff <= '0';
                    mem_state <= idle;
                end if;
            when others =>
                null;
            end case;
        end if;
    end process;

    tag_valid <= rtag(rtag'high);
    tag_dirty <= rtag(rtag'high-1);
    hit       <= '1' when ((wtag_d = rtag) and tag_valid = '1') else '0';
    do_write  <= write_d and not hit and tag_valid and tag_dirty;
    do_read   <= read_d and not hit;
    rack      <= client_req.request and not busy;
    dack      <= hit and read_d;
    tag_we    <= client_req.request and not client_req.read_writen;
    tag_en    <= client_req.request and not busy;
    data_we   <= tag_we;
    data_en   <= tag_en;
    busy      <= do_read or do_write or mem_busy;
    mem_busy  <= '0' when (mem_state = idle) else '1';
    
    -- constructing our response to the client
    client_resp.rack     <= rack;
    client_resp.rack_tag <= client_req.tag when rack='1' else (others => '0');
--    client_resp.dack     <= dack;
    client_resp.dack_tag <= last_tag when dack='1' else (others => '0');
    client_resp.data     <= rdata(client_resp.data'range);
    
    -- constructing the downstream request and handling the response
    mem_req.request     <= (do_write or do_read) when (mem_state = idle) else mem_req_ff;
    mem_req.read_writen <= do_read;
    mem_req.address     <= client_req.address when do_read='1' else
                           rtag(rtag'high-2 downto 0) & client_req.address(c_cache_size_bits-1 downto 0);
    mem_req.tag         <= client_req.tag; -- just pass
    mem_req.data        <= rdata; -- old data from cache.

    i_tag_ram: entity work.dpram
    generic map (
        g_width_bits            => c_total_tag_width,
        g_depth_bits            => c_tag_size_bits,
        g_read_first_a          => true,
        g_read_first_b          => false,
        g_storage               => "block" )
    port map (
        a_clock                 => clock,
        a_address               => tag_address,
        a_rdata                 => rtag,
        a_wdata                 => wtag,
        a_en                    => tag_en,
        a_we                    => tag_we,

        b_clock                 => clock,
        b_address               => (others => '0'),
        b_rdata                 => open,
        b_wdata                 => (others => '0'),
        b_en                    => '0',
        b_we                    => '0' );

    i_data_ram: entity work.dpram
    generic map (
        g_width_bits            => c_total_data_width,
        g_depth_bits            => c_cache_size_bits,
        g_read_first_a          => true,
        g_read_first_b          => false,
        g_storage               => "block" )
    port map (
        a_clock                 => clock,
        a_address               => data_address,
        a_rdata                 => rdata,
        a_wdata                 => wdata,
        a_en                    => data_en,
        a_we                    => data_we,

        b_clock                 => clock,
        b_address               => (others => '0'),
        b_rdata                 => b_rdata,
        b_wdata                 => b_wdata,
        b_en                    => b_data_en,
        b_we                    => b_data_we );

    -- at this moment 
    b_wdata   <= mem_resp.data;
    b_data_en <= mem_resp.next or mem_resp.dack;
    b_data_we <= mem_resp.dack;
    

end gideon;
