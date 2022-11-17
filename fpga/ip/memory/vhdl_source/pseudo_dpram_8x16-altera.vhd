LIBRARY altera_mf;
USE altera_mf.altera_mf_components.all;

architecture altera of pseudo_dpram_8x16 is
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
        address_a => std_logic_vector(wr_address),
        address_b => std_logic_vector(rd_address),
        clock0    => wr_clock,
        clock1    => rd_clock,
        data_a    => wr_data,
        data_b    => X"0000",
        rden_a    => '0',
        rden_b    => rd_en,
        wren_a    => wr_en,
        wren_b    => '0',
        q_a       => open,
        q_b       => rd_data );

end architecture;
