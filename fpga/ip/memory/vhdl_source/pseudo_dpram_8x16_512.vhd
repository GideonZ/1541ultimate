library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity pseudo_dpram_8x16_512 is
    port (
        rd_clock                : in  std_logic;
        rd_address              : in  unsigned(7 downto 0);
        rd_data                 : out std_logic_vector(15 downto 0);
        rd_en                   : in  std_logic := '1';

        wr_clock                : in  std_logic;
        wr_address              : in  unsigned(8 downto 0);
        wr_data                 : in  std_logic_vector(7 downto 0) := (others => '0');
        wr_en                   : in  std_logic := '0' );

end entity;
