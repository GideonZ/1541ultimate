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
	request		: in  std_logic;
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
	
	signal address_d		: unsigned(address_in'range) := (others => '0');
	signal request_d		: std_logic := '0';
	signal tag_address		: unsigned(g_index_bits-1 downto 0);
	signal hit_i			: std_logic;
	signal wtag				: std_logic_vector(c_tag_width-1 downto 0);
	signal rtag				: std_logic_vector(c_tag_width-1 downto 0);
	signal we, en			: std_logic;
	signal modify_d			: std_logic := '0';
	signal r_valid			: std_logic;
	signal r_dirty			: std_logic;
    signal match_address    : std_logic_vector(rtag'high-2 downto 0);
begin
	tag_address <= address_in(g_index_bits+g_line_size_bits-1 downto g_line_size_bits);

	-- only in case of an invalidate, we will store a zero in the valid bit
	-- in case of modify, we will store a '1' in the dirty bit
	wtag  <= not invalidate & modify & std_logic_vector(address_in(address_in'high downto c_tag_low));
	we	  <= allocate or modify or invalidate;
    en    <= we or request;
    
	r_valid	<= rtag(rtag'high);
	r_dirty <= rtag(rtag'high-1);
	
    match_address   <= rtag(rtag'high-2 downto 0);
    address_out     <= unsigned(match_address) & address_d(c_tag_low-1 downto 0);
	dirty           <= r_dirty;
	valid           <= r_valid;
	hit_i           <= '1' when r_valid='1' and (std_logic_vector(address_d(address_d'high downto c_tag_low)) = match_address) else '0';
	hit             <= hit_i and request_d;
	miss            <= not hit_i and request_d;
	error           <= modify_d and not hit_i;
	
	process(clock)
	begin
		if rising_edge(clock) then
			modify_d  <= modify and not allocate;
			address_d <= address_in;
			request_d <= request;
		end if;
	end process;
	
    i_ram: entity work.spram
    generic map (
        g_width_bits  => c_tag_width,
        g_depth_bits  => g_index_bits,
        g_read_first  => true,
        g_storage     => "block" )
    port map (
        clock         => clock,
        address       => tag_address,
        rdata         => rtag,
        wdata         => wtag,
        en            => en,
        we            => we );

end architecture;
