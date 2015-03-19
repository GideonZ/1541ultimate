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
    signal sys_enable    : std_logic;
    signal sys_bram_addr : std_logic_vector(10 downto 0);

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
        INIT_00 => X"A028A02783E60AA7E8C1A0CA0AC3C0180BFFA03783E60AA4A0C40AB9A0CA0AB2",
        INIT_01 => X"E84D0AB0C0262AB27893E026A0CA0AAEE00B83E683FD0AA4C0100BFDE9EAE8F7",
        INIT_02 => X"0AA4C8352AA7783FE84D08F5A0C40AC1A0CA0AC3E028A0CA0AB2C0262AA67893",
        INIT_03 => X"C03CE854C8420BFDA02883E60AA5A03EA0263AA55AA5E028C0312AA7783F83E6",
        INIT_04 => X"08ECE8E780ECE05AA03EE84D0AA6A02783E60AA7E8AA83E683FD0AA4E028A028",
        INIT_05 => X"C8630BFCC8420BFDC840E854B800A03783E6A03ED859783EB800C84E80EC5AA5",
        INIT_06 => X"C87E52A60BFEA0C488CA80CA4AC92AA70BFEA03783E61AA80BE6E05AE9EAE8F7",
        INIT_07 => X"C828E854A03EE074E8E7C87D2AA7783F80CA5AA5C07D08CA80CA0AB0A0260AA5",
        INIT_08 => X"E099C87E52A52AA7783FE8E7C87E52A52AA7783FC8900BFEC09A0BFCC8420BFD",
        INIT_09 => X"E8C1A030E84D0AB8A020A031A025C87E52A62AA7783FE8E7C87E52A62AA7783F",
        INIT_0A => X"E0B283FE0AA4C0B02AA6783FE05A83E62ACB0BE6E8E7E8E7A027D8A15AA50ABB",
        INIT_0B => X"80EC5AA508ECE8E7D0CB7839C0E10BFEA039A02980EC0ACAA0C40ABC83FE0AA5",
        INIT_0C => X"08F6A020A03183FE0AA60000B800A03EA0C488CA80CA4AC62AA7A0260BFEC8BA",
        INIT_0D => X"0AA7A030E0D1C8DB5AA50AC7A021C0DE80EC5AA508ECC8D35AA50AC7A03180EC",
        INIT_0E => X"00400045004600000000B800D8E85AA50ABDE0C1C8E180EC5AA508ECE8E780EC",
        INIT_0F => X"81E10BF281E00BF181DF0BF0B800C8FA0BF0004B15E000500055005500560050",
        INIT_10 => X"B80083F00AA483F209E183F709E6E91081E50BF681E40BF581E30BF481E20BF3",
        INIT_11 => X"A06309E5A06109DEA07009E4A07109E3D18B09DF81DC0AA481DB0AA7A06209E0",
        INIT_12 => X"C96F52B6C13B52B5C14252B4C14852A42AAA81E6D9247864C93509E5A06009DF",
        INIT_13 => X"09DFA0631AAD09E5E8E7C93052B12AAA81E6D9357864E11C81DB5AA5C16F09DB",
        INIT_14 => X"2ABA39E609DF81DB0AA781DD2ACD09E6E16FC91C2AB309DFC91C09DCE124A060",
        INIT_15 => X"49DD09DCA0724AA7C16109DDA07409DED956783DC1612AB609DF81DD0AA4C153",
        INIT_16 => X"783DD91C51E209DDD16FC16F81E159DD09E181DE4AB609DE81DF3ABA09DF81DC",
        INIT_17 => X"4AA781DD09E2D17E51E209E1A07409DE81E72AA7A07009E4A07109E3B800D96F",
        INIT_18 => X"19DE1AB4C98F09DDE972B80081E36AA409E381E449DD09E4A0734AA749E72ACC",
        INIT_19 => X"09E5E972C19F81E159DD09E181DE4AB609DEA06009DFD993783DA06309E5A061",
        INIT_1A => X"81E6D9AD7864E1CA52B6C1D252B4C1D852B5C1C052B12AAA81E6D9A17864C9AD",
        INIT_1B => X"E1D581DB5AA5C1CA09DBE1A1A0602AB709DFE8E7A0631AAD09E5C9CA52B12AAA",
        INIT_1C => X"18EB2AAB09E680EB2ABA09DFC98C09E181DC49DD09DC81DB0AA781DF3ABA09DF",
        INIT_1D => X"00000000000000000000E1B6C1C009E5E1A1A06009DFC1CA2AB309DFB80081E6",
        INIT_1E => X"80EB89E881E90AA481E80AC00000000000000000000000000000000000000000",
        INIT_1F => X"00000000B800C9EE52BF81E84AAC09E881E94AA509E9CA012ABEC9EE50EB89E8",
        INIT_20 => X"DA1F51FE59FF783B81FF88EB80EB4AA909E881FE88EB80EB4AA809E881DF0000",
        INIT_21 => X"783BB80090EB09FE80EB4AA409E881FEB80090EB09FE80EB4AA909E881FEE1F5",
        INIT_22 => X"81E36AA409E381E449E40A00E1F5CA4B2AB189E8CA312AB409DFC1F5EA92EA11",
        INIT_23 => X"09DFE910EA70EA2AEA2AEA2A81E36AA40BE481E44BE58200EA88C1F5EA85B800",
        INIT_24 => X"6AA40BE081E44BE30BE1E1F5EA8DA028EA9709DCEA9719E90A00C26509DCEA18",
        INIT_25 => X"83E34AAF0BE3A028EA9709DCEA9719E90BE3C26509DCEA1809DFE910EA7081E3",
        INIT_26 => X"E1F5A028EA9709E6EA9719E90AC8C1F552B42AAA09E6E1F583E30AA4C9F553E2",
        INIT_27 => X"09E881E288EB80EB4AA709E881E188EB80EB4AA609E881E088EB80EB4AA509E8",
        INIT_28 => X"2AC44AA50BFAB8008AA382A34AC20BFAB8003BFA0BFBB80081E588EB80EB4AB2",
        INIT_29 => X"2AC54AA50BF992A30AA282A34ABF0BF982A2B8003BF82AC54AA50BF9B80083FA",
        INIT_2A => X"0010000E00800008F7FF700000050004000300020001000000000000B80083F9",
        INIT_2B => X"0320FDFF0751005000200800004500A0FFBF4000300020000400000610000FA0",
        INIT_2C => X"0000000003FFFFFCFFFB007800F1FFF002EE00ED007F003F006603A0006102E0",
        
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
        ADDRA => sys_bram_addr,
		DIA   => sys_io_req_bram.data,
        DIPA  => "0",
		DOA   => bram_data );

    sys_bram_addr(10 downto 1) <= std_logic_vector(sys_io_req_bram.address(10 downto 1));
    sys_bram_addr(0) <= not sys_io_req_bram.address(0);
    
    sys_enable <= sys_io_req_bram.write or sys_io_req_bram.read;
    bram_reset <= not sys_enable;

    sys_io_resp_bram.data <= bram_data when sys_io_resp_bram.ack = '1' else X"00";
    sys_io_resp_bram.irq <= '0';
            
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
