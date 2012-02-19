library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- LUT/FF/S3S/BRAM: 260/111/136/1

library work;
use work.io_bus_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity iec_processor_io is
generic (
    g_mhz           : natural := 50);
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    req             : in  t_io_req;
    resp            : out t_io_resp;

    clk_o           : out std_logic;
    clk_i           : in  std_logic;
    data_o          : out std_logic;
    data_i          : in  std_logic;
    atn_o           : out std_logic;
    atn_i           : in  std_logic;
    srq_o           : out std_logic;
    srq_i           : in  std_logic );

end iec_processor_io;

architecture structural of iec_processor_io is
    signal proc_reset      : std_logic;
    
    -- instruction ram interface
    signal instr_addr      : unsigned(8 downto 0);
    signal instr_en        : std_logic;
    signal instr_data      : std_logic_vector(31 downto 0);

    -- software fifo interface
    signal up_fifo_get     : std_logic;
    signal up_fifo_put     : std_logic;
    signal up_fifo_din     : std_logic_vector(8 downto 0);
    signal up_fifo_dout    : std_logic_vector(8 downto 0);
    signal up_fifo_empty   : std_logic;
    signal up_fifo_full    : std_logic;
    signal up_dav          : std_logic;
    signal up_space        : std_logic;
    signal down_fifo_flush : std_logic;
    signal down_fifo_empty : std_logic;
    signal down_fifo_full  : std_logic;
    signal down_fifo_get   : std_logic;
    signal down_fifo_put   : std_logic;
    signal down_fifo_din   : std_logic_vector(8 downto 0);
    signal down_fifo_dout  : std_logic_vector(8 downto 0);
    signal down_dav        : std_logic;
    signal down_space      : std_logic;

    signal ram_en          : std_logic;
    signal ram_rdata       : std_logic_vector(7 downto 0);
    signal ram_sel         : std_logic;
    signal reg_rdata       : std_logic_vector(7 downto 0);
begin

    i_proc: entity work.iec_processor
    generic map (
        g_mhz           => g_mhz )
    port map (
        clock           => clock,
        reset           => proc_reset,
        
        -- instruction ram interface
        instr_addr      => instr_addr,
        instr_en        => instr_en,
        instr_data      => instr_data(29 downto 0),
    
        -- software fifo interface
        up_fifo_put     => up_fifo_put,
        up_fifo_din     => up_fifo_din,
        up_fifo_full    => up_fifo_full,
        
        down_fifo_empty => down_fifo_empty,
        down_fifo_get   => down_fifo_get,
        down_fifo_dout  => down_fifo_dout,
        down_fifo_flush => down_fifo_flush,
                
        clk_o           => clk_o,
        clk_i           => clk_i,
        data_o          => data_o,
        data_i          => data_i,
        atn_o           => atn_o,
        atn_i           => atn_i,
        srq_o           => srq_o,
        srq_i           => srq_i );

--    i_inst_ram: entity work.dpram
--    generic map (
--        g_width_bits    => 32,
--        g_depth_bits    => 11,
--        g_read_first_a  => false,
--        g_read_first_b  => false,
--        g_storage       => "block" )
--    port map (
--        a_clock         => clock,
--        a_address       => instr_addr,
--        a_rdata         => instr_data,
--        a_wdata         => X"00",
--        a_en            => instr_en,
--        a_we            => '0',
--
--        b_clock         => clock,
--        b_address       => req.address(10 downto 0),
--        b_rdata         => ram_rdata,
--        b_wdata         => req.data,
--        b_en            => ram_en,
--        b_we            => req.write );

    i_ram: RAMB16_S9_S36
    generic map (
        INIT_00 => X"002000000000000117C0001117C000101E5000091E5000081E50000D1F50000C",
        INIT_01 => X"000000023280140037C000501F50000935800D3B1F50000835800B5B00406100",
        INIT_02 => X"17C000111F800D000040610017C000111F80170029805E0008801C0000200000",
        INIT_03 => X"1F50000D1E50000C1980910017C064111F805E002A80170037C0001000406100",
        INIT_04 => X"1F50000C1B50000A001000000020000018802300000000031E50000A17C05001",
        INIT_05 => X"1E50000C17C0280117C000221980A60017C3E82017C064012A802E0017C00022",
        INIT_06 => X"1F50000C17C050010150000D1E50000C17C050011F50000C17C050010050000D",
        INIT_07 => X"0350000D1E50000C17C050011F50000C17C050010250000D1E50000C17C05001",
        INIT_08 => X"17C050011F50000C17C050010450000D1E50000C17C050011F50000C17C05001",
        INIT_09 => X"17C050010650000D1E50000C17C050011F50000C17C050010550000D1E50000C",
        INIT_0A => X"1F50000D17C050011F50000C17C050010750000D1E50000C17C050011F50000C",
        INIT_0B => X"1F50000D1F50000C2A80250017C05A011980A30017C3E82017C001011E50000C",
        INIT_0C => X"17C046011E50000D2A806B001950000A17C0C8101F50000D1880610017C00001",
        INIT_0D => X"17C0C81117C0C810198097001150000017C0C8111980940017C3E8101F50000D",
        INIT_0E => X"17C0C81117C0C81019809D001150000217C0C81117C0C81019809A0011500001",
        INIT_0F => X"17C0C81117C0C8101980A3001150000417C0C81117C0C8101980A00011500003",
        INIT_10 => X"17C0C81117C0C8101980A9001150000617C0C81117C0C8101980A60011500005",
        INIT_11 => X"002000000000000E2A809000003000001E50000D17C0C8101980AC0011500007",
        INIT_12 => X"000000E21F805E0000200000000000E11F805E0000200000000000FC00700000",
        INIT_13 => X"1F805E0000200000000000E41F805E0000200000000000E31F805E0000200000",
        INIT_14 => X"00200000000000E71F805E0000200000000000E61F805E0000200000000000E5",
        INIT_15 => X"000000001F805E0000200000000000E91F805E0000200000000000E81F805E00",
        INIT_16 => X"0000000000000000000000000000000000000000000000000000000000000000",
                                                
        WRITE_MODE_A => "WRITE_FIRST",
        WRITE_MODE_B => "WRITE_FIRST" )
    port map (
        DOA   => ram_rdata,
        ADDRA => std_logic_vector(req.address(10 downto 0)),
        CLKA  => clock,
        DIA   => req.data,
        DIPA  => "0",
        ENA   => ram_en,
        SSRA  => '0',
        WEA   => req.write,
       
        DOB   => instr_data,
        ADDRB => std_logic_vector(instr_addr),
        CLKB  => clock,
        DIB   => X"00000000",
        DIPB  => X"0",
        ENB   => instr_en,
        SSRB  => '0',
        WEB   => '0' );


    i_up_fifo: entity work.srl_fifo
    generic map (
        Width       => 9,
        Depth       => 15, -- 15 is the maximum
        Threshold   => 13 )
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => up_fifo_get,
        PutElement  => up_fifo_put,
        FlushFifo   => '0',
        DataIn      => up_fifo_din,
        DataOut     => up_fifo_dout,
        SpaceInFifo => up_space,
        AlmostFull  => open,
        DataInFifo  => up_dav);
    
    up_fifo_empty <= not up_dav;
    up_fifo_full  <= not up_space;

    i_down_fifo: entity work.srl_fifo
    generic map (
        Width       => 9,
        Depth       => 15, -- 15 is the maximum
        Threshold   => 13 )
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => down_fifo_get,
        PutElement  => down_fifo_put,
        FlushFifo   => down_fifo_flush,
        DataIn      => down_fifo_din,
        DataOut     => down_fifo_dout,
        SpaceInFifo => down_space,
        AlmostFull  => open,
        DataInFifo  => down_dav);

    down_fifo_empty <= not down_dav;
    down_fifo_full  <= not down_space;

    process(clock)
    begin
        if rising_edge(clock) then
            ram_sel <= '0';
            reg_rdata <= (others => '0');
            proc_reset <= '0';
            resp.ack <= '0';
                        
            if req.read='1' then
                resp.ack <= '1'; -- data handled outside clocked process if ram
                ram_sel <= req.address(11);
                case req.address(2 downto 0) is
                    when "000" =>
                        reg_rdata <= X"21"; -- version
                        
                    when "001" =>
                        reg_rdata(0) <= down_fifo_empty;
                        reg_rdata(1) <= down_fifo_full;
                    
                    when "010" =>
                        reg_rdata(0) <= up_fifo_empty;
                        reg_rdata(1) <= up_fifo_full;
                        reg_rdata(7) <= up_fifo_dout(8); 

                    when "110" =>
                        reg_rdata <= up_fifo_dout(7 downto 0);
                    
                    when "111" =>
                        reg_rdata <= "0000000" & up_fifo_dout(8);
                        
                    when others => null;
                end case;
            elsif req.write='1' then
                resp.ack <= '1'; -- data handled outside clocked process if ram
                if req.address(11)='0' then
                    case req.address(2 downto 0) is
                        when "011" =>
                            proc_reset <= '1';
                        when others =>
                            null;
                    end case;
                end if;
            end if;
            
            if reset='1' then
                proc_reset <= '1';
            end if;
        end if;

    end process;

    resp.data  <= ram_rdata when ram_sel='1' else reg_rdata;
    down_fifo_put <= '1' when req.write='1' and req.address(11)='0' and req.address(2 downto 1) = "10" else '0';
    up_fifo_get   <= '1' when req.read='1'  and req.address(11)='0' and req.address(2 downto 0) = "110" else '0';    
    down_fifo_din <= req.address(0) & req.data;
    ram_en <= (req.write or req.read) and req.address(11);
end structural;
