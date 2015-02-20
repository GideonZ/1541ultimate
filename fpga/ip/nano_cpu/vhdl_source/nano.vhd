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
        INIT_00 => X"A028A02783E60A60E8C3A0CA0A79C0190BFFA03783E60A5DA0C40A70A0CA0A69",
        INIT_01 => X"0A67C0272A697893E027A0CA0A65E00BEA5583E683FD0A5DC0100BFDE99AE8F9",
        INIT_02 => X"C8362A60783FE84F08F7A0C40A77A0CA0A79E029A0CA0A69C0272A5F7893E84F",
        INIT_03 => X"E856C8430BFDA02883E60A5EA03EA0263A5E5A5EE029C0322A60783F83E60A5D",
        INIT_04 => X"80EEE05CA03EE84F0A5FA02783E60A60E8ACEA5583E683FD0A5DE029A028C03D",
        INIT_05 => X"C8430BFDC841E856B800A03783E6A03ED85B783EB800C85080EE5A5E08EEE8E9",
        INIT_06 => X"0BFEA0C488CC80CC4A7E2A600BFEA03783E61A610BE6E05CE99AE8F9C8650BFC",
        INIT_07 => X"A03EE076E8E9C87F2A60783F80CC5A5EC07F08CC80CC0A67A0260A5EC880525F",
        INIT_08 => X"525E2A60783FE8E9C880525E2A60783FC8920BFEC09C0BFCC8430BFDC829E856",
        INIT_09 => X"E84F0A6FA020A031A025C880525F2A60783FE8E9C880525F2A60783FE09BC880",
        INIT_0A => X"0A5DC0B22A5F783FE05C83E62A800BE6E8E9E8E9A027D8A35A5E0A72E8C3A030",
        INIT_0B => X"08EEE8E9D0CD7839C0E30BFEA039A02980EE0A7FA0C40A7383FE0A5EE0B483FE",
        INIT_0C => X"A03183FE0A5F0000B800A03EA0C488CC80CC4A7C2A60A0260BFEC8BC80EE5A5E",
        INIT_0D => X"E0D3C8DD5A5E0A7BA021C0E080EE5A5E08EEC8D55A5E0A7BA03180EE08F8A020",
        INIT_0E => X"004600000000B800D8EA5A5E0A74E0C3C8E380EE5A5E08EEE8E980EE0A60A030",
        INIT_0F => X"81910BF181900BF0B800C8FC0BF0004B15E00050005500550056005000400045",
        INIT_10 => X"0A5D83F2099283F70997E91281960BF681950BF581940BF481930BF381920BF2",
        INIT_11 => X"A0600990A061098FD15B0990818D0A5DA0620991A0700995A0710994B80083F0",
        INIT_12 => X"818E2A810997E152C91C2A6A0990E152C129526BC12D525D2A638197D9207864",
        INIT_13 => X"4A60C144098EA074098FD939783DC1442A6D0990818E0A5DC1362A7139970990",
        INIT_14 => X"098ED152C1528192598E0992818F4A6D098F81903A710990818D498E098DA072",
        INIT_15 => X"198F1A6BC95F098EE955B800A0734A60E987A074098FB800D952783DD91C5193",
        INIT_16 => X"E955C1708192598E099281903A710990818F4A6D098FA0600990D961783DA061",
        INIT_17 => X"C95C0992818D498E098DE180C181526BC17B526CC17B52682A638197D9707864",
        INIT_18 => X"000000000000B800818E0993D18B51930992E170A0600990C1802A6A0990B800",
        INIT_19 => X"C9AC899881990A5D81980A760000000000000000000000000000000000000000",
        INIT_1A => X"80ED4A6109988190000000000000B800C99E527581984A64099881994A5E0999",
        INIT_1B => X"80ED4A62099881A9E1A0D9CA51A959AA783B81AA88ED80ED4A62099881A988ED",
        INIT_1C => X"2A6B0990C1A0EA45E9BC783BB80090ED09A980ED4A5D099881A9B80090ED09A9",
        INIT_1D => X"499509AB81946A5D0BE481954BE581ABEA3BC1A0EA38E1A0C9FE2A688998C9D5",
        INIT_1E => X"81946A5D09948195499509AB81946A5D09948195499509AB81946A5D09948195",
        INIT_1F => X"4BE30BE1E1A0EA40A028EA4A098DEA4A199909ABC218098DE9C30990E912EA23",
        INIT_20 => X"A028EA4A098DEA4A19990BE3C218098DE9C30990E912EA2381946A5D0BE08195",
        INIT_21 => X"0997EA4A19990A7DC1A0526B2A630997E1A083E30A5DC9A053E283E34A660BE3",
        INIT_22 => X"80ED4A600998819288ED80ED4A5F0998819188ED80ED4A5E0998E1A0A028EA4A",
        INIT_23 => X"B8008A5C825C4A780BFAB8003BFA0BFBB800819688ED80ED4A690998819388ED",
        INIT_24 => X"925C0A5B825C4A750BF9825BB8003BF82A7A4A5E0BF9B80083FA2A6E4A5E0BFA",
        INIT_25 => X"00020001000000000000B80083FB83FA83F983F80A5DB80083F92A7A4A5E0BF9",
        INIT_26 => X"00A0003F4000300020000400000610000FA00010000E00087000000500040003",
        INIT_27 => X"007800F3FFF000EF02EE007F006603A0006102E0032007510050002008000045",
        INIT_28 => X"0000000000000000000000000000000000000000000000000000000003FFFFFB",
        
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
