library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity dpram is
    generic (
        g_width_bits            : positive := 16;
        g_depth_bits            : positive := 9;
        g_read_first_a          : boolean := false;
        g_read_first_b          : boolean := false;
        g_storage               : string := "auto"  -- can also be "block" or "distributed"
    );
    port (
        a_clock                 : in  std_logic;
        a_address               : in  unsigned(g_depth_bits-1 downto 0);
        a_rdata                 : out std_logic_vector(g_width_bits-1 downto 0);
        a_wdata                 : in  std_logic_vector(g_width_bits-1 downto 0) := (others => '0');
        a_en                    : in  std_logic := '1';
        a_we                    : in  std_logic := '0';

        b_clock                 : in  std_logic := '0';
        b_address               : in  unsigned(g_depth_bits-1 downto 0) := (others => '0');
        b_rdata                 : out std_logic_vector(g_width_bits-1 downto 0);
        b_wdata                 : in  std_logic_vector(g_width_bits-1 downto 0) := (others => '0');
        b_en                    : in  std_logic := '1';
        b_we                    : in  std_logic := '0' );

    attribute keep_hierarchy : string;
    attribute keep_hierarchy of dpram : entity is "yes";

end entity;

architecture xilinx of dpram is
    type t_ram is array(0 to 2**g_depth_bits-1) of std_logic_vector(g_width_bits-1 downto 0);
    shared variable ram : t_ram := (others => (others => '0'));

    -- Xilinx and Altera attributes
    attribute ram_style        : string;
    attribute ram_style of ram : variable is g_storage;
begin
    -----------------------------------------------------------------------
    -- PORT A
    -----------------------------------------------------------------------
    p_port_a: process(a_clock)
    begin
        if rising_edge(a_clock) then
            if a_en = '1' then
                if g_read_first_a then
                    a_rdata <= ram(to_integer(a_address));
                end if;
                
                if a_we = '1' then
                    ram(to_integer(a_address)) := a_wdata;
                end if;

                if not g_read_first_a then
                    a_rdata <= ram(to_integer(a_address));
                end if;
            end if;
        end if;
    end process;

    -----------------------------------------------------------------------
    -- PORT B
    -----------------------------------------------------------------------
    p_port_b: process(b_clock)
    begin
        if rising_edge(b_clock) then
            if b_en = '1' then
                if g_read_first_b then
                    b_rdata <= ram(to_integer(b_address));
                end if;

                if b_we = '1' then
                    ram(to_integer(b_address)) := b_wdata;
                end if;

                if not g_read_first_b then
                    b_rdata <= ram(to_integer(b_address));
                end if;
            end if;
        end if;
    end process;

end architecture;
