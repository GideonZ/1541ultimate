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
        INIT_00 => X"A027A023E8B6A022A0CA08F2C0170BFFA037A034A033A032A0C408F1A0CA08FB",
        INIT_01 => X"6893E84808EDC02528FB6893E025A0CA08F6E00CA037A03383FD08ECC0100BFD",
        INIT_02 => X"683FA032C83328EF683FE84808EAA0C408F3A0CA08F2E027A0CA08FBC02528EE",
        INIT_03 => X"A033A03783FD08ECC0380BFDC827E84FA022A03EA02638F058F0E027C02F28EF",
        INIT_04 => X"683EB800C84980E158F008E1E8DC80E1E056A03EE84808EEA027A023A034E89F",
        INIT_05 => X"28EF0BFEA037A024C0560BFCC83C0BFDC827E84FB800A033A037A032A03ED855",
        INIT_06 => X"683F80BF58F0C07408BF80BF08EDA02608F0C87550EE0BFEA0C488BF80BF48FD",
        INIT_07 => X"50F028EF683FC8870BFEC0910BFCC83C0BFDC827E84FA03EE06BE8DCC87428EF",
        INIT_08 => X"C87550EE28EF683FE8DCC87550EE28EF683FE090C87550F028EF683FE8DCC875",
        INIT_09 => X"683FE056A034E8DCE8DCA027D89858F008F9E8B6A030E84808F4A020A031A025",
        INIT_0A => X"6839C0D60BFEA039A02980E108FAA0C408F783FE08F0E0A783FE08ECC0A528EE",
        INIT_0B => X"0000B800A03EA0C488BF80BF48F528EFA0260BFEC8AF80E158F008E1E8DCD0C0",
        INIT_0C => X"08FCA021C0D380E158F008E1C8C858F008FCA03180E108EBA020A03183FE08EE",
        INIT_0D => X"B800D8DD58F008F8E0B6C8D680E158F008E1E8DC80E108EFA030E0C6C8D058F0",
        INIT_0E => X"000300020FA00000004B15E00050005500550056005000400045004600000000",
        INIT_0F => X"0000000000E602EE00060078002007510050000E00E200A00061006600450001",
        INIT_10 => X"0000000000000000000000000000000000000000000000000000000000000000",
        
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
