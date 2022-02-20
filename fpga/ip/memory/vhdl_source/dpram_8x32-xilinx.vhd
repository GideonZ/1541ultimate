
library unisim;
use unisim.vcomponents.all;

architecture xilinx of dpram_8x32 is
begin
    ram: RAMB16_S9_S36
    port map (
        CLKA  => CLKA,
        SSRA  => SSRA,
        ENA   => ENA,
        WEA   => WEA,
        ADDRA => ADDRA,
        DIA   => DIA,
        DIPA  => "0",
        DOA   => DOA,
        DOPA  => open,
    
        CLKB  => CLKB,
        SSRB  => SSRB,
        ENB   => ENB,
        WEB   => WEB,
        ADDRB => ADDRB,
        DIB   => DIB,
        DIPB  => X"0",
        DOB   => DOB,
        DOPB  => open );

end architecture;
