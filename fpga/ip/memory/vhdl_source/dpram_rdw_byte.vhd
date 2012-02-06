library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library std;
    use std.textio.all;

library work;
    use work.tl_file_io_pkg.all;

entity dpram_rdw_byte is
    generic (
        g_rdw_check             : boolean := true;
        g_width_bits            : positive := 32;
        g_depth_bits            : positive := 9;
        g_init_file             : string := "none";
        g_storage               : string := "block"  -- can also be "block" or "distributed"
    );
    port (
        clock                   : in  std_logic;

        a_address               : in  unsigned(g_depth_bits-1 downto 0);
        a_rdata                 : out std_logic_vector(g_width_bits-1 downto 0);
        a_en                    : in  std_logic := '1';

        b_address               : in  unsigned(g_depth_bits-1 downto 0);
        b_rdata                 : out std_logic_vector(g_width_bits-1 downto 0);
        b_wdata                 : in  std_logic_vector(g_width_bits-1 downto 0) := (others => '0');
        b_byte_en               : in  std_logic_vector((g_width_bits/8)-1 downto 0) := (others => '1');
        b_en                    : in  std_logic := '1';
        b_we                    : in  std_logic := '0' );

--    attribute keep_hierarchy : string;
--    attribute keep_hierarchy of dpram_rdw_byte : entity is "yes";

end entity;

architecture xilinx of dpram_rdw_byte is
    signal b_we_i       : std_logic_vector(b_byte_en'range) := (others => '0');
begin
    assert (g_width_bits mod 8) = 0
        report "Width of ram with byte enables should be a multiple of 8."
        severity failure;
        
    b_we_i <= b_byte_en when b_we='1' else (others => '0');
    
    r_byte: for i in b_we_i'range generate

        i_byte_ram: entity work.dpram_rdw
        generic map (
            g_rdw_check   => g_rdw_check,
            g_width_bits  => 8,
            g_depth_bits  => g_depth_bits,
            g_init_value  => X"00",
            g_init_file   => g_init_file,
            g_init_width  => (g_width_bits/8),
            g_init_offset => i,
            g_storage     => g_storage )
        port map (
            clock         => clock,
    
            a_address     => a_address,
            a_rdata       => a_rdata(8*i+7 downto 8*i),
            a_en          => a_en,
    
            b_address     => b_address,
            b_rdata       => b_rdata(8*i+7 downto 8*i),
            b_wdata       => b_wdata(8*i+7 downto 8*i),
            b_en          => b_en,
            b_we          => b_we_i(i) );
    end generate;    
end architecture;
