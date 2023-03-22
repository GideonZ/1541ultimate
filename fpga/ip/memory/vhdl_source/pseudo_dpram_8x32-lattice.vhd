
library ECP5U;
use ECP5U.components.all;

architecture lattice of pseudo_dpram_8x32 is
begin
    ram: DP16KD
        generic map (
            INIT_DATA=> "STATIC",
            ASYNC_RESET_RELEASE=> "SYNC",
            CSDECODE_B      => "0b000",
            CSDECODE_A      => "0b000",
            WRITEMODE_B     => "NORMAL",
            WRITEMODE_A     => "NORMAL",
            GSR             => "ENABLED",
            RESETMODE       => "ASYNC",
            REGMODE_B       => "NOREG",
            REGMODE_A       => "NOREG",
            DATA_WIDTH_B    =>  36,
            DATA_WIDTH_A    =>  9)
        port map (
            DIA17 => '0',
            DIA16 => '0',
            DIA15 => '0',
            DIA14 => '0',
            DIA13 => '0',
            DIA12 => '0',
            DIA11 => '0',
            DIA10 => '0',
            DIA9  => '0',
            DIA8  => '0',
            DIA7  => wr_data(7),
            DIA6  => wr_data(6),
            DIA5  => wr_data(5),
            DIA4  => wr_data(4),
            DIA3  => wr_data(3),
            DIA2  => wr_data(2),
            DIA1  => wr_data(1),
            DIA0  => wr_data(0),
            ADA13 => wr_address(10),
            ADA12 => wr_address(9),
            ADA11 => wr_address(8),
            ADA10 => wr_address(7),
            ADA9  => wr_address(6),
            ADA8  => wr_address(5),
            ADA7  => wr_address(4),
            ADA6  => wr_address(3),
            ADA5  => wr_address(2),
            ADA4  => wr_address(1),
            ADA3  => wr_address(0),
            ADA2  => '0',
            ADA1  => '0',
            ADA0  => '0',
            CEA   => '1',
            OCEA  => '1',
            CLKA  => wr_clock,
            WEA   => wr_en,
            CSA2  => '0',
            CSA1  => '0',
            CSA0  => '0',
            RSTA  => '0',
            DIB17 => '0',
            DIB16 => '0',
            DIB15 => '0',
            DIB14 => '0',
            DIB13 => '0',
            DIB12 => '0',
            DIB11 => '0',
            DIB10 => '0',
            DIB9  => '0',
            DIB8  => '0',
            DIB7  => '0',
            DIB6  => '0',
            DIB5  => '0',
            DIB4  => '0',
            DIB3  => '0',
            DIB2  => '0',
            DIB1  => '0',
            DIB0  => '0',
            ADB13 => rd_address(8),
            ADB12 => rd_address(7),
            ADB11 => rd_address(6),
            ADB10 => rd_address(5),
            ADB9  => rd_address(4),
            ADB8  => rd_address(3),
            ADB7  => rd_address(2),
            ADB6  => rd_address(1),
            ADB5  => rd_address(0),
            ADB4  => '0',
            ADB3  => '0',
            ADB2  => '0',
            ADB1  => '0',
            ADB0  => '0',
            CEB   => rd_en,
            OCEB  => rd_en,
            CLKB  => rd_clock,
            WEB   => '0',
            CSB2  => '0',
            CSB1  => '0',
            CSB0  => '0',
            RSTB  => '0',
            DOA17 => open,
            DOA16 => rd_data(15),
            DOA15 => rd_data(14),
            DOA14 => rd_data(13),
            DOA13 => rd_data(12),
            DOA12 => rd_data(11),
            DOA11 => rd_data(10),
            DOA10 => rd_data(9),
            DOA9  => rd_data(8),
            DOA8  => open,
            DOA7  => rd_data(7),
            DOA6  => rd_data(6),
            DOA5  => rd_data(5),
            DOA4  => rd_data(4),
            DOA3  => rd_data(3),
            DOA2  => rd_data(2),
            DOA1  => rd_data(1),
            DOA0  => rd_data(0),
            DOB17 => open,
            DOB16 => rd_data(31),
            DOB15 => rd_data(30),
            DOB14 => rd_data(29),
            DOB13 => rd_data(28),
            DOB12 => rd_data(27),
            DOB11 => rd_data(26),
            DOB10 => rd_data(25),
            DOB9  => rd_data(24),
            DOB8  => open,
            DOB7  => rd_data(23),
            DOB6  => rd_data(22),
            DOB5  => rd_data(21),
            DOB4  => rd_data(20),
            DOB3  => rd_data(19),
            DOB2  => rd_data(18),
            DOB1  => rd_data(17),
            DOB0  => rd_data(16)
        );

end architecture;