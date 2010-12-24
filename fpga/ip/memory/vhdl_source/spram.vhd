library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity spram is
    generic (
        g_width_bits  : positive := 16;
        g_depth_bits  : positive := 9;
        g_read_first  : boolean := false;
        g_storage     : string := "auto"  -- can also be "block" or "distributed"
    );
    port (
        clock         : in  std_logic;
        address       : in  unsigned(g_depth_bits-1 downto 0);
        rdata         : out std_logic_vector(g_width_bits-1 downto 0);
        wdata         : in  std_logic_vector(g_width_bits-1 downto 0) := (others => '0');
        en            : in  std_logic := '1';
        we            : in  std_logic );

    attribute keep_hierarchy : string;
    attribute keep_hierarchy of spram : entity is "yes";

end entity;

architecture xilinx of spram is
    type t_ram is array(0 to 2**g_depth_bits-1) of std_logic_vector(g_width_bits-1 downto 0);
    shared variable ram : t_ram := (others => (others => '0'));

    -- Xilinx and Altera attributes
    attribute ram_style        : string;
    attribute ram_style of ram : variable is g_storage;
begin
    p_port: process(clock)
    begin
        if rising_edge(clock) then
            if en = '1' then
                if g_read_first then
                    rdata <= ram(to_integer(address));
                end if;
                
                if we = '1' then
                    ram(to_integer(address)) := wdata;
                end if;

                if not g_read_first then
                    rdata <= ram(to_integer(address));
                end if;
            end if;
        end if;
    end process;
end architecture;
