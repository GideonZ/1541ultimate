library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.nano_cpu_pkg.all;
use work.io_bus_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity nano is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    -- i/o interface
    io_addr     : out unsigned(7 downto 0);
    io_write    : out std_logic;
    io_read     : out std_logic;
    io_wdata    : out std_logic_vector(15 downto 0);
    io_rdata    : in  std_logic_vector(15 downto 0);
    stall       : in  std_logic;
    
    -- system interface (to write code into the nano)
    sys_clock   : in  std_logic := '0';
    sys_reset   : in  std_logic := '0';
    sys_io_req  : in  t_io_req := c_io_req_init;
    sys_io_resp : out t_io_resp );
end entity;

architecture structural of nano is
    signal sys_enable   : std_logic;

    -- instruction/data ram
    signal ram_addr    : std_logic_vector(9 downto 0);
    signal ram_en      : std_logic;
    signal ram_we      : std_logic;
    signal ram_wdata   : std_logic_vector(15 downto 0);
    signal ram_rdata   : std_logic_vector(15 downto 0);

    signal sys_io_req_bram  : t_io_req;
    signal sys_io_resp_bram : t_io_resp;
    signal sys_io_req_regs  : t_io_req;
    signal sys_io_resp_regs : t_io_resp;

    signal sys_core_reset   : std_logic;
    signal usb_reset_tig    : std_logic;
    signal usb_core_reset   : std_logic;    
    signal bram_reset       : std_logic;
    signal bram_data        : std_logic_vector(7 downto 0);
begin
    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 11,
        g_range_hi  => 11,
        g_ports     => 2 )
    port map (
        clock    => sys_clock,
        
        req      => sys_io_req,
        resp     => sys_io_resp,
        
        reqs(0)  => sys_io_req_bram, 
        reqs(1)  => sys_io_req_regs, 
        
        resps(0) => sys_io_resp_bram,
        resps(1) => sys_io_resp_regs );

    i_core: entity work.nano_cpu
    port map (
        clock       => clock,
        reset       => usb_core_reset,
        
        -- instruction/data ram
        ram_addr    => ram_addr,
        ram_en      => ram_en,
        ram_we      => ram_we,
        ram_wdata   => ram_wdata,
        ram_rdata   => ram_rdata,
    
        -- i/o interface
        io_addr     => io_addr,
        io_write    => io_write,
        io_read     => io_read,
        io_wdata    => io_wdata,
        io_rdata    => io_rdata,
        stall       => stall );

    i_buf_ram: RAMB16_S9_S18
    generic map (
        INIT_00 => X"A027A023E8BDA022A0CA0A5FC01A0BFFA037A034A033A032A0C40A56A0CA0A51",
        INIT_01 => X"C0282A517893E028A0CA0A4DE00CA037A033EA3D83FD0A45C0100BFDE985E8F3",
        INIT_02 => X"2A48783FE84C08F1A0C40A5DA0CA0A5FE02AA0CA0A51C0282A477893E84C0A4F",
        INIT_03 => X"0A45C03B0BFDC82AE853A022A03EA0263A465A46E02AC0322A48783FA032C836",
        INIT_04 => X"5A4608E8E8E380E8E05AA03EE84C0A47A027A023A034E8A6EA3DA033A03783FD",
        INIT_05 => X"C8630BFCC83F0BFDC82AE853B800A033A037A032A03ED859783EB800C84D80E8",
        INIT_06 => X"A0260A46C87C52470BFEA0C488C680C64A612A480BFEA037A024E05AE985E8F3",
        INIT_07 => X"C83F0BFDC82AE853A03EE072E8E3C87B2A48783F80C65A46C07B08C680C60A4F",
        INIT_08 => X"2A48783FE097C87C52462A48783FE8E3C87C52462A48783FC88E0BFEC0980BFC",
        INIT_09 => X"5A460A58E8BDA030E84C0A55A020A031A025C87C52472A48783FE8E3C87C5247",
        INIT_0A => X"A0C40A5983FE0A46E0AE83FE0A45C0AC2A47783FE05AA034E8E3E8E3A027D89F",
        INIT_0B => X"2A48A0260BFEC8B680E85A4608E8E8E3D0C77839C0DD0BFEA039A02980E80A64",
        INIT_0C => X"5A460A62A03180E808F2A020A03183FE0A470000B800A03EA0C488C680C64A60",
        INIT_0D => X"5A4608E8E8E380E80A48A030E0CDC8D75A460A62A021C0DA80E85A4608E8C8CF",
        INIT_0E => X"005500550056005000400045004600000000B800D8E45A460A5AE0BDC8DD80E8",
        INIT_0F => X"817F0BF4817E0BF3817D0BF2817C0BF1817B0BF0B800C8F60BF0004B15E00050",
        INIT_10 => X"A0700980A071097FB80083F00A4583F2097D83F70982E90C81810BF681800BF5",
        INIT_11 => X"C12152452A538182D91A7864A060097BA061097AD14F097B81780A45A062097C",
        INIT_12 => X"097AD92D783DC1382A4B097B81790A45C12A2A573982097B81792A660982E146",
        INIT_13 => X"5979097D817A4A4B097A817B3A57097B817849790978A0724A48C1380979A074",
        INIT_14 => X"E949B800A0734A48E972A074097AB800D946783DD916517E0979D146C146817D",
        INIT_15 => X"097D817B3A57097B817A4A4B097AA060097BD955783DA061197A1A52C9530979",
        INIT_16 => X"D96E7864E150817849790978C94652502A538182D9647864E949C16E817D5979",
        INIT_17 => X"00000000000000000000000000000000B8008179097ED176517E097DB8008182",
        INIT_18 => X"4A4C098381844A460984C997898381840A4581830A5C00000000000000000000",
        INIT_19 => X"80E74A4A0983819488E780E74A490983817B000000000000B800C989525B8183",
        INIT_1A => X"09838194B80090E7099480E74A4A09838194E18BD9B551945995783B819588E7",
        INIT_1B => X"E18BC9E82A508983C9C02A52097BC18BEA2DE9A7783BB80090E7099480E74A45",
        INIT_1C => X"0996817F6A45097F818049800996817F6A450BE481804BE58196EA23C18BEA20",
        INIT_1D => X"0978E9AE097BE90CEA0B817F6A45097F818049800996817F6A45097F81804980",
        INIT_1E => X"E90CEA0B817F6A450BE081804BE30BE1E18BEA28EA320982EA3219840996C201",
        INIT_1F => X"83E30A45C98B53E283E34A4E0BE3EA320978EA3219840BE3C2010978E9AE097B",
        INIT_20 => X"817C88E780E74A460983E18BEA320982EA3219840A63C18B52522A530982E18B",
        INIT_21 => X"B800818188E780E74A510983817E88E780E74A480983817D88E780E74A470983",
        INIT_22 => X"2A654A460BF9B80083FA2A544A460BFAB8008A4482444A5E0BFAB8003BFA0BFB",
        INIT_23 => X"83F983F80A45B80083F92A654A460BF992440A4382444A5B0BF98243B8003BF8",
        INIT_24 => X"0FA00010000E0008400000050004000300020001000000000000B80083FB83FA",
        INIT_25 => X"006603A0006102E003200751005000200800004500A0003F7000200000061000",
        INIT_26 => X"00000000000000000000000000000000000003FF007F0078FFF002EE00ED00E9",
        
        INIT_3F => X"0000000000000000000000000000000000000000000000000000000000000000"
    )
    port map (
		CLKB  => clock,
		SSRB  => reset,
		ENB   => ram_en,
		WEB   => ram_we,
        ADDRB => ram_addr,
		DIB   => ram_wdata,
		DIPB  => "00",
		DOB   => ram_rdata,
		
		CLKA  => sys_clock,
		SSRA  => sys_reset,
		ENA   => sys_enable,
		WEA   => sys_io_req_bram.write,
        ADDRA => std_logic_vector(sys_io_req_bram.address(10 downto 0)),
		DIA   => sys_io_req_bram.data,
        DIPA  => "0",
		DOA   => bram_data );

    sys_enable <= sys_io_req_bram.write or sys_io_req_bram.read;
    bram_reset <= not sys_enable;

    sys_io_resp_bram.data <= bram_data when sys_io_resp_bram.ack = '1' else X"00";
        
    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            sys_io_resp_bram.ack <= sys_enable;
            
            sys_io_resp_regs <=  c_io_resp_init;            
            sys_io_resp_regs.ack <= sys_io_req_regs.write or sys_io_req_regs.read;

            if sys_io_req_regs.write = '1' then -- any address
                sys_core_reset <= not sys_io_req_regs.data(0);
            end if;

            if sys_reset = '1' then
                sys_core_reset <= '1';
            end if;
        end if;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            usb_reset_tig  <= sys_core_reset;
            usb_core_reset <= usb_reset_tig;
        end if;
    end process;

end architecture;
