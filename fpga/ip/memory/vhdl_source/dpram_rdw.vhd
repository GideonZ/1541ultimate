library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library std;
    use std.textio.all;

library work;
    use work.tl_file_io_pkg.all;
    
entity dpram_rdw is
    generic (
        g_rdw_check             : boolean := true;
        g_width_bits            : positive := 8;
        g_depth_bits            : positive := 10;
        g_init_value            : std_logic_vector := X"22";
        g_init_file             : string := "none";
        g_init_width            : integer := 1;
        g_init_offset           : integer := 0;
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

    impure function read_file (filename : string; modulo : integer; offset : integer; ram_size : integer) return t_ram is
        constant c_read_size : integer := (4 * modulo * ram_size) + offset;
        variable mem    : t_slv8_array(0 to c_read_size-1) := (others => (others => '0'));
        variable result : t_ram := (others => g_init_value);
        variable stat   : file_open_status;
        file myfile     : text;
    begin
        if filename /= "none" then
            file_open(stat, myfile, filename, read_mode);
            assert (stat = open_ok)
                report "Could not open file " & filename & " for reading."
                severity failure;
            read_hex_file_to_array(myfile, c_read_size, mem);
            file_close(myfile);
            if g_width_bits = 8 then
                for i in 0 to ram_size-1 loop
                    result(i) := mem(i*modulo + offset);
                end loop;            
            elsif g_width_bits = 16 then
                for i in 0 to ram_size-1 loop
                    result(i)(15 downto 8) := mem(i*modulo*2 + offset);
                    result(i)( 7 downto 0) := mem(i*modulo*2 + offset + 1);
                end loop;            
            elsif g_width_bits = 32 then
                for i in 0 to ram_size-1 loop
                    result(i)(31 downto 24) := mem(i*modulo*4 + offset);
                    result(i)(23 downto 16) := mem(i*modulo*4 + offset + 1);
                    result(i)(15 downto  8) := mem(i*modulo*4 + offset + 2);
                    result(i)( 7 downto  0) := mem(i*modulo*4 + offset + 3);
                end loop;            
            else
                report "Unsupported width for initialization."
                severity failure;
            end if;
        end if;
        return result;
    end function;

    shared variable ram : t_ram := read_file(g_init_file, g_init_width, g_init_offset, 2**g_depth_bits);
--    shared variable ram : t_ram := (others => g_init_value);

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
                    if a_en='1' and (a_address = b_address) and g_rdw_check then
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
