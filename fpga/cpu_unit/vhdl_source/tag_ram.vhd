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
	
	query_addr  : in  unsigned(g_address_width-1 downto 0);
    do_query    : in  std_logic;

    update_addr : in  unsigned(g_address_width-1 downto 0);
	allocate	: in  std_logic; -- clears dirty bit, sets valid bit, stores address
	modify		: in  std_logic; -- sets dirty bit
	invalidate	: in  std_logic; -- clears valid bit

	error		: out std_logic; -- tried to modify while there was a cache miss
	dirty		: out std_logic;
    valid       : out std_logic;
	miss		: out std_logic;
	hit			: out std_logic;
	
	address_out : out unsigned(g_address_width-1 downto 0));
end tag_ram;

architecture gideon of tag_ram is
	constant c_tag_width	: natural := 2 + g_address_width - g_index_bits - g_line_size_bits; -- in example: 17
	constant c_tag_low		: natural := g_index_bits + g_line_size_bits;
	
	signal address_d		: unsigned(query_addr'range) := (others => '0');
	signal do_query_d		: std_logic := '0';
	signal tag_query_idx	: unsigned(g_index_bits-1 downto 0);
	signal tag_update_idx   : unsigned(g_index_bits-1 downto 0);
	signal hit_i			: std_logic;
	signal wtag				: std_logic_vector(c_tag_width-1 downto 0);
	signal wtag_d			: std_logic_vector(c_tag_width-1 downto 0);
	signal rtag				: std_logic_vector(c_tag_width-1 downto 0);
	signal rtag_raw			: std_logic_vector(c_tag_width-1 downto 0);
	signal we   			: std_logic;
	signal modify_d			: std_logic := '0';
	signal r_valid			: std_logic;
	signal r_dirty			: std_logic;
    signal match_address    : std_logic_vector(rtag'high-2 downto 0);
    signal bypass_d         : boolean;
begin
	tag_query_idx  <= query_addr(g_index_bits+g_line_size_bits-1 downto g_line_size_bits);
    tag_update_idx <= update_addr(g_index_bits+g_line_size_bits-1 downto g_line_size_bits);

	-- only in case of an invalidate, we will store a zero in the valid bit
	-- in case of modify, we will store a '1' in the dirty bit
	wtag  <= not invalidate & modify & std_logic_vector(update_addr(update_addr'high downto c_tag_low));
	we	  <= allocate or modify or invalidate;
    
	r_valid	<= rtag(rtag'high);
	r_dirty <= rtag(rtag'high-1);
	
    match_address   <= rtag(rtag'high-2 downto 0);
    address_out     <= unsigned(match_address) & address_d(c_tag_low-1 downto 0);
	dirty           <= r_dirty;
	valid           <= r_valid;
	hit_i           <= '1' when r_valid='1' and (std_logic_vector(address_d(address_d'high downto c_tag_low)) = match_address) else '0';
	hit             <= hit_i and do_query_d;
	miss            <= not hit_i and do_query_d;
	error           <= modify_d and not hit_i;
	
	process(clock)
	begin
		if rising_edge(clock) then
			modify_d  <= modify and not allocate;
			address_d <= query_addr;
			do_query_d <= do_query;
            bypass_d  <= (tag_update_idx = tag_query_idx) and (we='1');
            wtag_d    <= wtag;
		end if;
	end process;
	
    rtag <= wtag_d when bypass_d else rtag_raw;
    
    i_ram: entity work.dpram
    generic map (
        g_width_bits  => c_tag_width,
        g_depth_bits  => g_index_bits,
        g_storage     => "block" )
    port map (
        a_clock       => clock,
        a_address     => tag_query_idx,
        a_rdata       => rtag_raw,
        a_en          => do_query,

        b_clock       => clock,
        b_address     => tag_update_idx,
        b_rdata       => open,
        b_wdata       => wtag,
        b_en          => we,
        b_we          => we );

end architecture;
