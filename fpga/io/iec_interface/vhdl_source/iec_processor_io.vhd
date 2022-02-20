library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- LUT/FF/S3S/BRAM: 260/111/136/1

library work;
use work.io_bus_pkg.all;

entity iec_processor_io is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    tick            : in  std_logic;
    
    req             : in  t_io_req;
    resp            : out t_io_resp;
    irq             : out std_logic;
    
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
    signal up_fifo_count   : integer range 0 to 2048;
    signal down_fifo_flush : std_logic;
    signal down_fifo_empty : std_logic;
    signal down_fifo_full  : std_logic;
    signal down_fifo_release : std_logic;
    signal down_fifo_get   : std_logic;
    signal down_fifo_put   : std_logic;
    signal down_fifo_din   : std_logic_vector(9 downto 0);
    signal down_fifo_dout  : std_logic_vector(9 downto 0);
    signal down_dav        : std_logic;
    signal down_space      : std_logic;

    signal ram_write       : std_logic;
    signal reg_rdata       : std_logic_vector(7 downto 0);
begin

    i_proc: entity work.iec_processor
    port map (
        clock           => clock,
        reset           => proc_reset,
        tick            => tick,
                
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
        down_fifo_release => down_fifo_release,
        
        irq_event       => irq_event,

        clk_o           => clk_o,
        clk_i           => clk_i,
        data_o          => data_o,
        data_i          => data_i,
        atn_o           => atn_o,
        atn_i           => atn_i,
        srq_o           => srq_o,
        srq_i           => srq_i );

    i_ram: entity work.pseudo_dpram_8x32
    port map (
        rd_clock   => clock,
        rd_address => instr_addr,
        rd_data(31 downto 24) => instr_data(7 downto 0), -- CPU is big endian
        rd_data(23 downto 16) => instr_data(15 downto 8),
        rd_data(15 downto 8) => instr_data(23 downto 16),
        rd_data(7 downto 0) => instr_data(31 downto 24),
        rd_en      => instr_en,

        wr_clock   => clock,
        wr_address => req.address(10 downto 0),
        wr_data    => req.data,
        wr_en      => ram_write
    );

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
    
            flush       => proc_reset,
    
            full        => up_fifo_full,
            almost_full => open,
            empty       => up_fifo_empty,
            count       => up_fifo_count  );

    i_down_fifo: entity work.srl_fifo
    generic map (
        Width       => 10,
        Depth       => 15, -- 15 is the maximum
        Threshold   => 13 )
    port map (
        clock       => clock,
        reset       => proc_reset,
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
            down_fifo_release <= '0';
            reg_rdata <= (others => '0');
            proc_reset <= not enable;
            resp.ack <= '0';
            
            if req.read='1' then
                resp.ack <= '1'; -- data handled outside clocked process if ram
                case req.address(3 downto 0) is
                    when X"0" =>
                        reg_rdata <= X"25"; -- version
                        
                    when X"1" =>
                        reg_rdata(0) <= down_fifo_empty;
                        reg_rdata(1) <= down_fifo_full or down_fifo_flush;
                    
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
                        when X"D" =>
                            down_fifo_release <= '1';
                        when others =>
                            null;
                    end case;
                end if;
            end if;
            if irq_event='1' then
                irq_status <= '1';
            end if;
            irq <= irq_enable and irq_status;
            
            if reset='1' then
                proc_reset <= '1';
                enable <= '0';
            end if;
        end if;

    end process;

    ram_write     <= req.write and req.address(11);
    resp.data     <= reg_rdata;
    down_fifo_put <= '1' when req.write='1' and req.address(11)='0' and req.address(3 downto 2) = "10" else '0';
    up_fifo_get   <= '1' when req.read='1'  and req.address(11)='0' and ((req.address(3 downto 0) = "0110") or (req.address(3 downto 2) = "10")) else '0';    
    down_fifo_din <= std_logic_vector(req.address(1 downto 0)) & req.data;
end structural;
