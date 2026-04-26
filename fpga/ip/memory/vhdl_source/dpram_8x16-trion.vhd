library efxphysicallib;
use efxphysicallib.efxcomponents.all;

architecture trion of dpram_8x16 is
begin
    i_ram0 : EFX_DPRAM_5K
    generic map (
        READ_WIDTH_A => 2,
        WRITE_WIDTH_A => 2,
        READ_WIDTH_B => 4,
        WRITE_WIDTH_B => 4,
        WRITE_MODE_A => "READ_FIRST",
        WRITE_MODE_B => "READ_FIRST"
    )
    port map (
        WDATAA    => DIA(1 downto 0),
        ADDRA  => ADDRA,
        CLKA   => CLKA,
        CLKEA  => ENA,
        WEA    => WEA,
        RDATAA => DOA(1 downto 0),

        WDATAB(3 downto 2) => DIB(9 downto 8),
        WDATAB(1 downto 0) => DIB(1 downto 0),
        ADDRB  => ADDRB,
        CLKB   => CLKB,
        CLKEB  => ENB,
        WEB    => WEB,
        RDATAB(3 downto 2) => DOB(9 downto 8),
        RDATAB(1 downto 0) => DOB(1 downto 0)
    );

    i_ram1 : EFX_DPRAM_5K
    generic map (
        READ_WIDTH_A => 2,
        WRITE_WIDTH_A => 2,
        READ_WIDTH_B => 4,
        WRITE_WIDTH_B => 4,
        WRITE_MODE_A => "READ_FIRST",
        WRITE_MODE_B => "READ_FIRST"
    )
    port map (
        WDATAA    => DIA(3 downto 2),
        ADDRA  => ADDRA,
        CLKA   => CLKA,
        CLKEA  => ENA,
        WEA    => WEA,
        RDATAA => DOA(3 downto 2),

        WDATAB(3 downto 2) => DIB(11 downto 10),
        WDATAB(1 downto 0) => DIB(3 downto 2),
        ADDRB  => ADDRB,
        CLKB   => CLKB,
        CLKEB  => ENB,
        WEB    => WEB,
        RDATAB(3 downto 2) => DOB(11 downto 10),
        RDATAB(1 downto 0) => DOB(3 downto 2)
    );

    i_ram2 : EFX_DPRAM_5K
    generic map (
        READ_WIDTH_A => 2,
        WRITE_WIDTH_A => 2,
        READ_WIDTH_B => 4,
        WRITE_WIDTH_B => 4,
        WRITE_MODE_A => "READ_FIRST",
        WRITE_MODE_B => "READ_FIRST"
    )
    port map (
        WDATAA    => DIA(5 downto 4),
        ADDRA  => ADDRA,
        CLKA   => CLKA,
        CLKEA  => ENA,
        WEA    => WEA,
        RDATAA => DOA(5 downto 4),

        WDATAB(3 downto 2) => DIB(13 downto 12),
        WDATAB(1 downto 0) => DIB(5 downto 4),
        ADDRB  => ADDRB,
        CLKB   => CLKB,
        CLKEB  => ENB,
        WEB    => WEB,
        RDATAB(3 downto 2) => DOB(13 downto 12),
        RDATAB(1 downto 0) => DOB(5 downto 4)
    );

    i_ram3 : EFX_DPRAM_5K
    generic map (
        READ_WIDTH_A => 2,
        WRITE_WIDTH_A => 2,
        READ_WIDTH_B => 4,
        WRITE_WIDTH_B => 4,
        WRITE_MODE_A => "READ_FIRST",
        WRITE_MODE_B => "READ_FIRST"
    )
    port map (
        WDATAA => DIA(7 downto 6),
        ADDRA  => ADDRA,
        CLKA   => CLKA,
        CLKEA  => ENA,
        WEA    => WEA,
        RDATAA => DOA(7 downto 6),

        WDATAB(3 downto 2) => DIB(15 downto 14),
        WDATAB(1 downto 0) => DIB(7 downto 6),
        ADDRB  => ADDRB,
        CLKB   => CLKB,
        CLKEB  => ENB,
        WEB    => WEB,
        RDATAB(3 downto 2) => DOB(15 downto 14),
        RDATAB(1 downto 0) => DOB(7 downto 6)
    );

end architecture;
