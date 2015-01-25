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
        INIT_00 => X"096CE011A0CA095CC00F29596893E8210968C00F295C6893E93FA0CA0963E947",
        INIT_01 => X"E9340964E05CE9340894E9340965E8DAC01E295A6832E821089AA0C40974A0CA",
        INIT_02 => X"D0356824B800A027D82A6827B800C822809559580895A02FD822682F8095E020",
        INIT_03 => X"E02AA011A0000959B8000957E033A020D83A6820A024B8000958E850D82E682F",
        INIT_04 => X"C856295D808E4958088EE05051590894A02FD856682FE02AA010A001088FA000",
        INIT_05 => X"0BF2C0840BF0E845B800E9340966A02DD85B682DE82AA010A000095BA0016860",
        INIT_06 => X"0BF4E082C079516BC074516AC06F51690BF0808F0BF183F50957A0700BF3A071",
        INIT_07 => X"D87E687483F5A0726833811FE8C4E082E934E8A0E8850BF4E082E934E89CE885",
        INIT_08 => X"00000000B800A050B800D8896874A040A073C08C8090E05C83F00957E934091F",
        INIT_09 => X"E0A6095AE8400962004B15E00050004000450046000000000000000000000008",
        INIT_0A => X"C0BE6822C8BEE82EE82AA012A00259580890A000E0A6195A295E0891E8400958",
        INIT_0B => X"49580892B8000958C8BE5163B8000959C8BA5160B800095AC8B651596830A022",
        INIT_0C => X"C8D85161E0D20957E83CC8CE515A0830C8D9E82EE840095FE0B8C0BC515A8092",
        INIT_0D => X"8094095AC0E1515E296F6832E0BEE0B6B8000959B800095AC8D65093095EE83C",
        INIT_0E => X"0894A02EA01E80950976A0C4097280940958E0E980940957C0E729596832B800",
        INIT_0F => X"B800A0C48900810049670894C8F1809559580895A02FD8F1682FD101682EC117",
        INIT_10 => X"A055C114809559580895C90959580975A0458095089BA018A045809409590000",
        INIT_11 => X"0000E0FAC917809559580895A02FD917682F8095095AA019E107C91159580975",
        INIT_12 => X"0BFDB800894683FE297149580BFE8146496D0BFEB800C926E920B8003BFE0BFF",
        INIT_13 => X"0957B80083FD297749580BFD91460945814649700BFD8145B8003BFC29774958",
        INIT_14 => X"0969A07183F30973A07083F2096E83F4096200000000B80083FF83FE83FD83FC",
        INIT_15 => X"000900080007000600050003000200010000B800A0720963809B809A095B83F0",
        INIT_16 => X"0038123403B000260023002200210FA00096001300120011000E000D000B000A",
        INIT_17 => X"00000000000000000000000000000000007F007802EE006156780050003F0330",
        
        INIT_3F => X"FF00000000000000000000000000000000000000000000000000000000000000"
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
