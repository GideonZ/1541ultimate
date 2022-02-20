
library unisim;
use unisim.vcomponents.all;

architecture xilinx of pseudo_dpram_8x16 is
begin
    ram: RAMB16_S9_S18
    port map (
        CLKA  => wr_clock,
        SSRA  => '0',
        ENA   => wr_en,
        WEA   => wr_en,
        ADDRA => std_logic_vector(wr_address),
        DIA   => wr_data,
        DIPA  => "0",
        DOA   => open,
        DOPA  => open,
    
        CLKB  => rd_clock,
        SSRB  => '0',
        ENB   => rd_en,
        WEB   => '0',
        ADDRB => std_logic_vector(rd_address),
        DIB   => X"0000",
        DIPB  => "00",
        DOB   => rd_data,
        DOPB  => open );

end architecture;
