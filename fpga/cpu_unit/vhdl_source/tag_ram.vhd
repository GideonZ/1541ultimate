library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tag_ram is
generic (
	g_address_width		: natural := 26;
	g_line_size_bits	: natural := 2;  -- 4 words per line (entry)
	g_index_bits		: natural := 9 );  -- 512 entries
port (
	clock		: in  std_logic;
	reset		: in  std_logic;
	
	address_in  : in  unsigned(g_address_width-1 downto 0);
    do_query    : in  std_logic;

	allocate	: in  std_logic; -- clears/sets dirty bit, sets valid bit, stores address
    alloc_dirty : in  std_logic; -- determines if the line is allocated on a read or write miss
	invalidate	: in  std_logic; -- clears valid bit

    modify_addr : in  unsigned(g_address_width-1 downto 0);
	modify		: in  std_logic; -- sets dirty bit

	dirty		: out std_logic;
    valid       : out std_logic;
	miss		: out std_logic;
	hit			: out std_logic;
	
	address_out : out unsigned(g_address_width-1 downto 0));
end tag_ram;

architecture gideon of tag_ram is
	constant c_tag_width	: natural := 1 + g_address_width - g_index_bits - g_line_size_bits; -- in example: 16
	constant c_tag_low		: natural := g_index_bits + g_line_size_bits;
	
	signal address_d		: unsigned(address_in'range) := (others => '0');
	signal do_query_d		: std_logic := '0';
	signal tag_query_idx	: unsigned(g_index_bits-1 downto 0);
	signal tag_modify_idx   : unsigned(g_index_bits-1 downto 0);
	signal hit_i			: std_logic;
	signal wtag				: std_logic_vector(c_tag_width-1 downto 0);
	signal rtag				: std_logic_vector(c_tag_width-1 downto 0);
	signal we, en  			: std_logic;
	signal r_valid			: std_logic;
	signal r_dirty			: std_logic;
    signal r_dirty_raw      : std_logic;
    signal dirty_bypass     : std_logic := '0';
    signal match_address    : std_logic_vector(rtag'high-1 downto 0);
    signal bypass_d         : boolean;
begin
	tag_query_idx  <= address_in (g_index_bits+g_line_size_bits-1 downto g_line_size_bits);
    tag_modify_idx <= modify_addr(g_index_bits+g_line_size_bits-1 downto g_line_size_bits);
    
	-- only in case of an invalidate, we will store a zero in the valid bit
	-- in case of modify, we will store a '1' in the dirty bit
	wtag  <= not invalidate & std_logic_vector(address_in(address_in'high downto c_tag_low));
	we	  <= allocate or invalidate;
    en    <= we or do_query;
        
	r_valid	<= rtag(rtag'high);
	
    match_address   <= rtag(rtag'high-1 downto 0);
    address_out     <= unsigned(match_address) & address_d(c_tag_low-1 downto 0);
	dirty           <= r_dirty;
	valid           <= r_valid;
	hit_i           <= '1' when r_valid='1' and (std_logic_vector(address_d(address_d'high downto c_tag_low)) = match_address) else '0';
	hit             <= hit_i and do_query_d;
	miss            <= not hit_i and do_query_d;
	
	process(clock)
	begin
		if rising_edge(clock) then
			address_d <= address_in;
			do_query_d <= do_query;

            if modify='1' and tag_modify_idx = tag_query_idx then
                dirty_bypass <= '1';
            else
                dirty_bypass <= '0';
            end if;
		end if;
	end process;
	
    i_ram: entity work.spram
    generic map (
        g_width_bits  => c_tag_width,
        g_depth_bits  => g_index_bits,
        g_storage     => "block" )
    port map (
        clock       => clock,
        address     => tag_query_idx,
        wdata       => wtag,
        rdata       => rtag,
        en          => en,
        we          => we );

    b_dirty: block
        signal dirty_we     : std_logic;
        signal dirty_waddr  : unsigned(tag_modify_idx'range);
        signal dirty_wdata  : std_logic;
    begin
    
        dirty_we    <= modify or allocate or invalidate;
        dirty_waddr <= tag_modify_idx when modify='1' else tag_query_idx;
        dirty_wdata <= modify or (alloc_dirty and allocate);
        
        i_dirty: entity work.pseudo_dpram
        generic map (
            g_width_bits  => 1,
            g_depth_bits  => g_index_bits,
            g_read_first  => false,
            g_storage     => "distributed" )
        port map (
            clock         => clock,
            rd_address    => tag_query_idx,
            rd_data(0)    => r_dirty,
            rd_en         => '1',

            wr_address    => dirty_waddr,            
            wr_data(0)    => dirty_wdata,
            wr_en         => dirty_we );

    end block;
--        p_dirty_ram: process(clock)
--            variable dirty_bits : std_logic_vector(0 to 2**g_index_bits - 1) := (others => '0');
--            attribute ram_style        : string;
--            attribute ram_style of dirty_bits : variable is "distributed";
--        begin
--            if rising_edge(clock) then
----              if modify='1' then
----                  dirty_bits(to_integer(tag_modify_idx)) := '1';
----              elsif allocate='1' then
----                  dirty_bits(to_integer(tag_query_idx)) := alloc_dirty;
----              elsif invalidate='1' then
----                  dirty_bits(to_integer(tag_query_idx)) := '0';
----              end if;
--                if dirty_we='1' then
--                    dirty_bits(dirty_waddr) := dirty_wdata;
--                end if;
--                
--                r_dirty <= dirty_bits(to_integer(tag_query_idx));
--            end if;
--        end process;
    
--    i_dirty: entity work.dpram
--    generic map (
--        g_width_bits  => 1,
--        g_depth_bits  => g_index_bits,
--        g_storage     => "distributed" )
--    port map (
--        a_clock       => clock,
--        a_address     => tag_query_idx,
--        a_rdata(0)    => r_dirty_raw,
--        a_wdata(0)    => alloc_dirty,
--        a_en          => en,
--        a_we          => we,
--        
--        b_clock       => clock,
--        b_address     => tag_modify_idx,
--        b_wdata       => "1",
--        b_en          => modify,
--        b_we          => modify );
--
--    r_dirty <= r_dirty_raw when dirty_bypass='0' else '1'; -- OR should work too.
    
end architecture;
