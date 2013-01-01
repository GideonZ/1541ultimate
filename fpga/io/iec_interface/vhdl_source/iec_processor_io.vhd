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
    signal enable          : std_logic;

    -- irq
    signal irq_event       : std_logic;
    signal irq_enable      : std_logic;
    signal irq_status      : std_logic;
            
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
                
        irq_event       => irq_event,

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
        INIT_00 => X"00406F0019C00011002000000000004119C000101E5000111F5000101F80B200",
        INIT_01 => X"098012000A8012000040A10000409C002A800D001F800D0000409C0029800B00",
        INIT_02 => X"2A80690009801E0000200000000000423680060039C000501F50001119C04601",
        INIT_03 => X"1B80AF0019C0C8111F80690019C046012C80180000409C0000406F0019C00011",
        INIT_04 => X"004029001E50000C00200000000000431E50000C19C050011F5000111E500010",
        INIT_05 => X"2C80290000403100001000001F50000C188030103D802E00001000001F806900",
        INIT_06 => X"1E5000101B80AF0019C3E82019C064012C80390019C000221F50001000700000",
        INIT_07 => X"1E50001019C050011F50001019C05001005000111E50001019C0280119C00022",
        INIT_08 => X"1F50001019C05001025000111E50001019C050011F50001019C0500101500011",
        INIT_09 => X"045000111E50001019C050011F50001019C05001035000111E50001019C05001",
        INIT_0A => X"19C050011F50001019C05001055000111E50001019C050011F50001019C05001",
        INIT_0B => X"19C05001075000111E50001019C050011F50001019C05001065000111E500010",
        INIT_0C => X"19C05A011B80AF0019C3E82019C002011E5000101F50001119C050011F500010",
        INIT_0D => X"1A806F001F80B2001F5000111F5000101F50001200200000000000DD00700000",
        INIT_0E => X"19C3E8101F50001119C046011E5000112C8079001B50000C19C0C8101F500011",
        INIT_0F => X"1B80AF001550000119C0C81119C0C8101B80AF001550000019C0C8111B80AF00",
        INIT_10 => X"1B80AF001550000319C0C81119C0C8101B80AF001550000219C0C81119C0C810",
        INIT_11 => X"1B80AF001550000519C0C81119C0C8101B80AF001550000419C0C81119C0C810",
        INIT_12 => X"1B80AF001550000719C0C81119C0C8101B80AF001550000619C0C81119C0C810",
        INIT_13 => X"00200000000000452C80A00000300000007000001E50001119C0140119C0C810",
        INIT_14 => X"007000001E50000A1F5000093880A84A007000001E5000093880A45F00700000",
        INIT_15 => X"000000EE007000001E5000091F50000A3880AE2A007000001E50000A3880AB3F",
        INIT_16 => X"1880B94D1880D7573D80B200001000001F500008009000001F80690000200000",
        INIT_17 => X"1B80D40019C3E82019C014011F5000111E5000101E5000121E5000081F80B200",
        INIT_18 => X"1880694B1880CB4C1880B94D1F80C000004031001E50000C1D80C50000100000",
        INIT_19 => X"1F5000121F80C00019C06401004029001F50001200200000000000E51880CF4A",
        INIT_1A => X"002000001F80690000200000000000E61F80180019C005011F50001019C01E01",
        INIT_1B => X"00300000000000550010000000200000000000DA3480EE0039C000331E500008",
        INIT_1C => X"0040F900004120000040F900004120000040F9001F50001019C000221E500010",
        INIT_1D => X"00200000000000DE1F80D7000060000019C0003300200000000000AD00412500",
        INIT_1E => X"2780B2000060000019C00033004102000040F9001F50001119C000111E500011",
        INIT_1F => X"0060000000200000000000AE0070000019C0040119C3E83019C000331F80D700",
        INIT_20 => X"1F50001319C0080135500001345000031E50001319C015011F80B20019C00033",
        INIT_21 => X"355000043450000619C00801355000053450000719C008013550000034500002",
        INIT_22 => X"0041120000700000004102000041020000410200004102000070000000300000",
        INIT_23 => X"0070000000411200004112000041120000411200004112000041120000411200",
        INIT_24 => X"0041120000411700004117000070000000411700004117000041170000411700",
        INIT_25 => X"0000000000000000000000000000000000000000000000000000000000700000",
                                                                                
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


--    i_up_fifo: entity work.srl_fifo
--    generic map (
--        Width       => 9,
--        Depth       => 15, -- 15 is the maximum
--        Threshold   => 13 )
--    port map (
--        clock       => clock,
--        reset       => reset,
--        GetElement  => up_fifo_get,
--        PutElement  => up_fifo_put,
--        FlushFifo   => '0',
--        DataIn      => up_fifo_din,
--        DataOut     => up_fifo_dout,
--        SpaceInFifo => up_space,
--        AlmostFull  => open,
--        DataInFifo  => up_dav);
--    
--    up_fifo_empty <= not up_dav;
--    up_fifo_full  <= not up_space;

    i_up_fifo: entity work.sync_fifo
        generic map (
            g_depth        => 2048,
            g_data_width   => 9,
            g_threshold    => 500,
            g_storage      => "blockram",     -- can also be "blockram" or "distributed"
            g_fall_through => true )
        port map (
            clock       => clock,
            reset       => reset,
    
            rd_en       => up_fifo_get,
            wr_en       => up_fifo_put,
    
            din         => up_fifo_din,
            dout        => up_fifo_dout,
    
            flush       => '0',
    
            full        => up_fifo_full,
            almost_full => open,
            empty       => up_fifo_empty,
            count       => open  );

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
            proc_reset <= not enable;
            resp.ack <= '0';
                        
            if req.read='1' then
                resp.ack <= '1'; -- data handled outside clocked process if ram
                ram_sel <= req.address(11);
                case req.address(3 downto 0) is
                    when X"0" =>
                        reg_rdata <= X"23"; -- version
                        
                    when X"1" =>
                        reg_rdata(0) <= down_fifo_empty;
                        reg_rdata(1) <= down_fifo_full;
                    
                    when X"2" =>
                        reg_rdata(0) <= up_fifo_empty;
                        reg_rdata(1) <= up_fifo_full;
                        reg_rdata(7) <= up_fifo_dout(8); 

                    when X"6"|X"8"|X"9"|X"A"|X"B" =>
                        reg_rdata <= up_fifo_dout(7 downto 0);
                    
                    when X"7" =>
                        reg_rdata <= "0000000" & up_fifo_dout(8);
                        
                    when X"C" =>
                        reg_rdata <= "0000000" & irq_status;

                    when others => null;
                end case;
            elsif req.write='1' then
                resp.ack <= '1'; -- data handled outside clocked process if ram
                if req.address(11)='0' then
                    case req.address(3 downto 0) is
                        when X"3" =>
                            proc_reset <= '1';
                            enable <= req.data(0);
                        when X"C" =>
                            irq_status <= '0';
                            irq_enable <= req.data(0);
                        when others =>
                            null;
                    end case;
                end if;
            end if;
            if irq_event='1' then
                irq_status <= '1';
            end if;
            resp.irq <= irq_enable and irq_status;
            
            if reset='1' then
                proc_reset <= '1';
                enable <= '0';
            end if;
        end if;

    end process;

    resp.data  <= ram_rdata when ram_sel='1' else reg_rdata;
    down_fifo_put <= '1' when req.write='1' and req.address(11)='0' and req.address(3 downto 1) = "10" else '0';
    up_fifo_get   <= '1' when req.read='1'  and req.address(11)='0' and ((req.address(3 downto 0) = "0110") or (req.address(3 downto 2) = "10")) else '0';    
    down_fifo_din <= req.address(0) & req.data;
    ram_en <= (req.write or req.read) and req.address(11);
end structural;
