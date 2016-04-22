library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

LIBRARY altera_mf;
USE altera_mf.altera_mf_components.all;

entity dpram is
    generic (
        g_width_bits            : positive := 16;
        g_depth_bits            : positive := 9;
        g_read_first_a          : boolean := true;
        g_read_first_b          : boolean := true;
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


end entity;

architecture xilinx of dpram is
    function string_select(a,b: string; s:boolean) return string is
    begin
        if s then
            return a;
        end if;
        return b;
    end function;

-- Error (14271): Illegal value NEW_DATA for port_a_read_during_write_mode parameter in WYSIWYG primitive "ram_block1a0" -- value must be new_data_no_nbe_read, new_data_with_nbe_read or old_data

    constant a_new_data : string := string_select("OLD_DATA", "new_data_with_nbe_read", g_read_first_a);
    constant b_new_data : string := string_select("OLD_DATA", "new_data_with_nbe_read", g_read_first_b);

    signal wren_a   : std_logic;
    signal wren_b   : std_logic;
begin
    altsyncram_component : altsyncram
    GENERIC MAP (
        address_reg_b => "CLOCK1",
        clock_enable_input_a => "BYPASS",
        clock_enable_input_b => "BYPASS",
        clock_enable_output_a => "BYPASS",
        clock_enable_output_b => "BYPASS",
        indata_reg_b => "CLOCK1",
        intended_device_family => "Cyclone IV E",
        lpm_type => "altsyncram",
        numwords_a => 2 ** g_depth_bits,
        numwords_b => 2 ** g_depth_bits,
        operation_mode => "BIDIR_DUAL_PORT",
        outdata_aclr_a => "NONE",
        outdata_aclr_b => "NONE",
        outdata_reg_a => "UNREGISTERED",
        outdata_reg_b => "UNREGISTERED",
        power_up_uninitialized => "FALSE",
        read_during_write_mode_port_a => a_new_data,
        read_during_write_mode_port_b => b_new_data,
        widthad_a => g_depth_bits,
        widthad_b => g_depth_bits,
        width_a => g_width_bits,
        width_b => g_width_bits,
        width_byteena_a => 1,
        width_byteena_b => 1,
        wrcontrol_wraddress_reg_b => "CLOCK1"
    )
    PORT MAP (
        address_a => std_logic_vector(a_address),
        address_b => std_logic_vector(b_address),
        clock0    => a_clock,
        clock1    => b_clock,
        data_a    => a_wdata,
        data_b    => b_wdata,
        rden_a    => a_en,
        rden_b    => b_en,
        wren_a    => wren_a,
        wren_b    => wren_b,
        q_a       => a_rdata,
        q_b       => b_rdata );

    wren_a <= a_we and a_en;
    wren_b <= b_we and b_en;

end architecture;
