library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity pseudo_dpram is
    generic (
        g_width_bits            : positive := 16;
        g_depth_bits            : positive := 9;
        g_read_first            : boolean := false;
        g_storage               : string := "auto"  -- can also be "block" or "distributed"
    );
    port (
        clock                   : in  std_logic;
        rd_address              : in  unsigned(g_depth_bits-1 downto 0);
        rd_data                 : out std_logic_vector(g_width_bits-1 downto 0);
        rd_en                   : in  std_logic := '1';

        wr_address              : in  unsigned(g_depth_bits-1 downto 0);
        wr_data                 : in  std_logic_vector(g_width_bits-1 downto 0) := (others => '0');
        wr_en                   : in  std_logic := '0' );

    attribute keep_hierarchy : string;
    attribute keep_hierarchy of pseudo_dpram : entity is "yes";

end entity;

architecture xilinx of pseudo_dpram is
    type t_ram is array(0 to 2**g_depth_bits-1) of std_logic_vector(g_width_bits-1 downto 0);
    shared variable ram : t_ram := (others => (others => '0'));

    -- Xilinx and Altera attributes
    attribute ram_style        : string;
    attribute ram_style of ram : variable is g_storage;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if g_read_first then
                if rd_en='1' then
                    rd_data <= ram(to_integer(rd_address));
                end if;
            end if;
            
            if wr_en='1' then
                ram(to_integer(wr_address)) := wr_data;
            end if;
            
            if not g_read_first then
                if rd_en='1' then
                    rd_data <= ram(to_integer(rd_address));
                end if;
            end if;
        end if;
    end process;
end architecture;
