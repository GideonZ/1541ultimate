library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity pseudo_dpram_8x32 is
    port (
        rd_clock    : in  std_logic;
        rd_address  : in  unsigned(8 downto 0);
        rd_data     : out std_logic_vector(31 downto 0);
        rd_en       : in  std_logic := '1';

        wr_clock    : in  std_logic;
        wr_address  : in  unsigned(10 downto 0);
        wr_data     : in  std_logic_vector(7 downto 0) := (others => '0');
        wr_en       : in  std_logic := '0' );

end entity;
