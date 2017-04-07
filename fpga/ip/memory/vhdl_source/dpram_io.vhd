library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library work;
	use work.io_bus_pkg.all;
	
entity dpram_io is
    generic (
        g_depth_bits            : positive := 9;
        g_default               : std_logic_vector(7 downto 0) := X"0F";
        g_read_first_a          : boolean := false;
        g_read_first_b          : boolean := false;
        g_storage               : string := "auto"  -- can also be "block" or "distributed"
    );
    port (
        a_clock                 : in  std_logic;
        a_address               : in  unsigned(g_depth_bits-1 downto 0);
        a_rdata                 : out std_logic_vector(7 downto 0);
        a_wdata                 : in  std_logic_vector(7 downto 0) := (others => '0');
        a_en                    : in  std_logic := '1';
        a_we                    : in  std_logic := '0';

        b_clock                 : in  std_logic;
		b_req					: in  t_io_req;
		b_resp					: out t_io_resp );

    attribute keep_hierarchy : string;
    attribute keep_hierarchy of dpram_io : entity is "yes";

end entity;

architecture altera of dpram_io is
    signal b_en                : std_logic := '1';
    signal b_we                : std_logic := '0';
    signal b_rdata             : std_logic_vector(7 downto 0);
    signal b_ack               : std_logic := '0';
begin
    i_dpram: entity work.dpram
    generic map (
        g_width_bits   => 8,
        g_depth_bits   => g_depth_bits,
        g_read_first_a => g_read_first_a,
        g_read_first_b => g_read_first_b,
        g_storage      => g_storage
    )
    port map(
        a_clock        => a_clock,
        a_address      => a_address,
        a_rdata        => a_rdata,
        a_wdata        => a_wdata,
        a_en           => a_en,
        a_we           => a_we,

        b_clock        => b_clock,
        b_address      => b_req.address(g_depth_bits-1 downto 0),
        b_rdata        => b_rdata,
        b_wdata        => b_req.data,
        b_en           => b_en,
        b_we           => b_we
    );

    b_we        <= b_req.write;
    b_en        <= b_req.write or b_req.read;
    b_resp.data <= b_rdata when b_ack='1' else (others => '0');
    b_resp.ack  <= b_ack;

    process(b_clock)
    begin
        if rising_edge(b_clock) then
            b_ack <= b_en;
        end if;
    end process;
    
end architecture;

--architecture xilinx of dpram_io is
--  
--    type t_ram is array(0 to 2**g_depth_bits-1) of std_logic_vector(7 downto 0);
--    shared variable ram : t_ram := (others => g_default);
--
--    -- Xilinx and Altera attributes
--    attribute ram_style        : string;
--    attribute ram_style of ram : variable is g_storage;
--
--    signal b_address           : unsigned(g_depth_bits-1 downto 0);
--    signal b_rdata             : std_logic_vector(7 downto 0);
--    signal b_wdata             : std_logic_vector(7 downto 0) := (others => '0');
--    signal b_en                : std_logic := '1';
--    signal b_we                : std_logic := '0';
--  signal b_ack               : std_logic := '0';
--begin
--    -----------------------------------------------------------------------
--    -- PORT A
--    -----------------------------------------------------------------------
--    p_port_a: process(a_clock)
--    begin
--        if rising_edge(a_clock) then
--            if a_en = '1' then
--                if g_read_first_a then
--                    a_rdata <= ram(to_integer(a_address));
--                end if;
--                
--                if a_we = '1' then
--                    ram(to_integer(a_address)) := a_wdata;
--                end if;
--
--                if not g_read_first_a then
--                    a_rdata <= ram(to_integer(a_address));
--                end if;
--            end if;
--        end if;
--    end process;
--
--    -----------------------------------------------------------------------
--    -- PORT B
--    -----------------------------------------------------------------------
--  b_address <= b_req.address(g_depth_bits-1 downto 0);
--  b_we      <= b_req.write;
--  b_en      <= b_req.write or b_req.read;
--  b_wdata   <= b_req.data;
--  
--  b_resp.data <= b_rdata when b_ack='1' else (others => '0');
--  b_resp.ack  <= b_ack;
--   
--    p_port_b: process(b_clock)
--    begin
--        if rising_edge(b_clock) then
--          b_ack <= b_en;
--            if b_en = '1' then
--                if g_read_first_b then
--                    b_rdata <= ram(to_integer(b_address));
--                end if;
--
--                if b_we = '1' then
--                    ram(to_integer(b_address)) := b_wdata;
--                end if;
--
--                if not g_read_first_b then
--                    b_rdata <= ram(to_integer(b_address));
--                end if;
--            end if;
--        end if;
--    end process;
--
--end architecture;
