
LIBRARY altera_mf;
USE altera_mf.altera_mf_components.all;

architecture altera of dpram_8x16 is
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
        numwords_a => 2048,
        numwords_b => 1024,
        operation_mode => "BIDIR_DUAL_PORT",
        outdata_aclr_a => "NONE",
        outdata_aclr_b => "NONE",
        outdata_reg_a => "UNREGISTERED",
        outdata_reg_b => "UNREGISTERED",
        power_up_uninitialized => "FALSE",
        read_during_write_mode_port_a => "NEW_DATA_NO_NBE_READ",
        read_during_write_mode_port_b => "NEW_DATA_NO_NBE_READ",
        widthad_a => 11,
        widthad_b => 10,
        width_a => 8,
        width_b => 16,
        width_byteena_a => 1,
        width_byteena_b => 1,
        wrcontrol_wraddress_reg_b => "CLOCK1"
    )
    PORT MAP (
        address_a => ADDRA,
        address_b => ADDRB,
        clock0    => CLKA,
        clock1    => CLKB,
        data_a    => DIA,
        data_b    => DIB,
        rden_a    => ENA,
        rden_b    => ENB,
        wren_a    => wren_a,
        wren_b    => wren_b,
        q_a       => DOA,
        q_b       => DOB );

    wren_a <= WEA and ENA;
    wren_b <= WEB and ENB;

end architecture;
