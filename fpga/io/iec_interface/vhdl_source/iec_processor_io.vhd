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
        INIT_00 => X"001000000000000100C0001100C00A011F5000081E5000081E50000D1F50000C",
        INIT_01 => X"1F800F0000405C00298059001F80180008801C00001000000000000232801500",
        INIT_02 => X"00C064012A802B0000C000221F50000C1B50000A003000001E50000A00C05001",
        INIT_03 => X"0250000D1E50000C00C03C011F50000C00C03C010150000D1E50000C00C03C01",
        INIT_04 => X"00C03C010550000D1E50000C00C03C011F50000C00C03C010450000D1E50000C",
        INIT_05 => X"1980860000C3E8201E50000C1F50000D00C03C011F50000C00C03C010750000D",
        INIT_06 => X"00C0C810198086001150000100C0C81100C0C810198086001150000000C0C811",
        INIT_07 => X"00C0C810198086001150000500C0C81100C0C810198086001150000400C0C811",
        INIT_08 => X"00100000000000FC00700000001000000000000E2A808500002000001E50000D",
        INIT_09 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_0A => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_0B => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_0C => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_0D => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_0E => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_0F => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_10 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_11 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_12 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_13 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_14 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_15 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_16 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_17 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_18 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_19 => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_1A => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_1B => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_1C => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_1D => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_1E => X"0000000000000000000000000000000000000000000000000000000000000000",
        INIT_1F => X"0000000000000000000000000000000000000000000000000000000000000000",
        WRITE_MODE_A => "WRITE_FIRST",
        WRITE_MODE_B => "WRITE_FIRST" )
    port map (
        DOA   => ram_rdata,
        ADDRA => std_logic_vector(req.address(10 downto 0)),
        CLKA  => clock,
        DIA   => req.data,
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


    i_up_fifo: entity work.SRL_fifo
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

    i_down_fifo: entity work.SRL_fifo
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
                case req.address(2 downto 0) is
                    when "011" =>
                        proc_reset <= '1';
                    when others =>
                        null;
                end case;
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

end structural;
