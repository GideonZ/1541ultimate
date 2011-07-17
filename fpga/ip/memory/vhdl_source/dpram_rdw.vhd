library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity dpram_rdw is
    generic (
        g_width_bits            : positive := 16;
        g_depth_bits            : positive := 9;
        g_storage               : string := "auto"  -- can also be "block" or "distributed"
    );
    port (
        clock                   : in  std_logic;

        a_address               : in  unsigned(g_depth_bits-1 downto 0);
        a_rdata                 : out std_logic_vector(g_width_bits-1 downto 0);
        a_en                    : in  std_logic := '1';

        b_address               : in  unsigned(g_depth_bits-1 downto 0);
        b_rdata                 : out std_logic_vector(g_width_bits-1 downto 0);
        b_wdata                 : in  std_logic_vector(g_width_bits-1 downto 0) := (others => '0');
        b_en                    : in  std_logic := '1';
        b_we                    : in  std_logic := '0' );

--    attribute keep_hierarchy : string;
--    attribute keep_hierarchy of dpram_rdw : entity is "yes";

end entity;

architecture xilinx of dpram_rdw is
    type t_ram is array(0 to 2**g_depth_bits-1) of std_logic_vector(g_width_bits-1 downto 0);
    shared variable ram : t_ram := (others => (others => '0'));

    signal a_rdata_i    : std_logic_vector(a_rdata'range) := (others => '0');
    signal b_wdata_d    : std_logic_vector(b_wdata'range) := (others => '0');
    signal rdw_hazzard  : std_logic := '0';
    
    -- Xilinx and Altera attributes
    attribute ram_style        : string;
    attribute ram_style of ram : variable is g_storage;
begin
    p_ports: process(clock)
    begin
        if rising_edge(clock) then
            if a_en = '1' then
                a_rdata_i <= ram(to_integer(a_address));
                rdw_hazzard <= '0';
            end if;

            if b_en = '1' then
                if b_we = '1' then
                    ram(to_integer(b_address)) := b_wdata;
                    if a_en='1' and (a_address = b_address) then
                        b_wdata_d <= b_wdata;
                        rdw_hazzard <= '1';
                    end if;
                end if;
                b_rdata <= ram(to_integer(b_address));
            end if;
        end if;
    end process;

    a_rdata <= a_rdata_i when rdw_hazzard='0' else b_wdata_d;
end architecture;
