-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : eth_transmit
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Filter block for ethernet frames
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.io_bus_pkg.all;
    use work.block_bus_pkg.all;
    use work.mem_bus_pkg.all;
        
entity eth_transmit is
generic (
    g_mem_tag       : std_logic_vector(7 downto 0) := X"12" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    -- io interface for local cpu
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;

    -- interface to memory (read)
    mem_req         : out t_mem_req_32;
    mem_resp        : in  t_mem_resp_32;

    ----
    eth_clock       : in std_logic;
    eth_reset       : in std_logic;

    eth_tx_data     : out std_logic_vector(7 downto 0);
    eth_tx_sof      : out std_logic := '0';
    eth_tx_eof      : out std_logic;
    eth_tx_valid    : out std_logic;
    eth_tx_ready    : in  std_logic );

end entity;


architecture gideon of eth_transmit is

    type t_state is (idle);
    signal state    : t_state;
    signal address_valid    : std_logic;
    signal start_addr       : unsigned(25 downto 0);
    signal trigger          : std_logic;

    signal wr_en            : std_logic;
    signal wr_din           : std_logic_vector(8 downto 0);
    signal wr_full          : std_logic;
    
    -- memory signals
    signal mem_addr     : unsigned(25 downto 0) := (others => '0');
    signal read_req     : std_logic;
    
    -- signals in eth_clock domain
    signal eth_rd_next  : std_logic;
    signal eth_rd_dout  : std_logic_vector(8 downto 0);
    signal eth_rd_valid : std_logic;
    signal eth_rd_next  : std_logic;
    signal eth_trigger  : std_logic;
    signal eth_streaming: std_logic;
begin
    -- 9 bit wide fifo; 8 data bits and 1 control bit: eof

    i_fifo: entity work.async_fifo_ft
    generic map (
        --g_fast       => true,
        g_data_width => 9,
        g_depth_bits => 10
    )
    port map (
        wr_clock     => clock,
        wr_reset     => reset,
        wr_en        => wr_en,
        wr_din       => wr_din,
        wr_full      => wr_full,

        rd_clock     => eth_clock,
        rd_reset     => eth_reset,
        rd_next      => eth_rd_next,
        rd_dout      => eth_rd_dout,
        rd_valid     => eth_rd_valid
    );

    eth_rd_next  <= eth_tx_ready and eth_rd_valid and eth_streaming;
    eth_tx_valid <= eth_rd_valid and eth_streaming;
    eth_tx_data  <= eth_rd_dout(7 downto 0);
    eth_tx_eof   <= eth_rd_dout(8);
        
    i_sync : entity work.pulse_synchronizer
    port map (
        clock_in  => clock,
        pulse_in  => trigger,
        clock_out => eth_clock,
        pulse_out => eth_trigger
    );
    
    process(eth_clock)
        variable pkts : natural range 0 to 15;
    begin
        if rising_edge(eth_clock) then
            if eth_rd_next = '1' and eth_rd_dout(8) = '1' then -- eof 
                pkts := pkts - 1;
                if pkts = 0 then
                    eth_streaming <= '0';
                end if;
            end if;
            
            if eth_trigger = '1' then
                pkts := pkts + 1;
                eth_streaming <= '1';
            end if;
                
            if eth_reset = '1' then
                pkts := 0;
                eth_streaming <= '0';
            end if;                
        end if;
    end process;

    --- interface to ethernet clock domain is a fifo and a trigger:
    -- trigger is given when end of packet is pushed into the fifo, OR when the 512th byte of a packet is pushed in the fifo.
    -- From the software point of view, the interface is simple. There is a busy flag, when clear, the software writes
    -- the address to read from, and the length, and the copying into the fifo begins. When the copy is completed, the busy
    -- flag is reset and IRQ is raised. 
    process(clock)
        alias local_addr : unsigned(3 downto 0) is io_req.address(3 downto 0);
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;

            start <= '0';
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case local_addr is
                when X"0" =>
                    null;
                when X"1" =>
                    sw_addr(15 downto 8) <= io_req.data;
                when X"2" =>
                    sw_addr(23 downto 16) <= io_req.data;
                when X"3" => 
                    sw_addr(25 downto 24) <= io_req.data(1 downto 0);
                when X"4" =>
                    sw_length(7 downto 0) <= io_req.data;
                when X"5" =>
                    sw_length(11 downto 8) <= io_req.data(3 downto 0);
                when X"8" =>
                    start <= '1';
                    busy <= '1';    
                when X"9" =>
                    io_irq <= '0';
                    
                when others =>
                    null; 
                end case;
            end if;

            if io_req.read = '1' then
                io_resp.ack <= '1';
                case local_addr is
                when X"A" =>
                    io_resp.data(0) <= busy;
                when others =>
                    null; 
                end case;
            end if;

            if reset = '1' then
                busy <= '0';
                io_irq <= '0';
            end if;
        end if;
    end process;

    -- condense to do 16 bit compares with data from fifo
    my_mac_16(0) <= my_mac(1) & my_mac(0);
    my_mac_16(1) <= my_mac(3) & my_mac(2);
    my_mac_16(2) <= my_mac(5) & my_mac(4);
    
    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                mac_idx <= 0;
                toggle <= '0';
                for_me <= '1';
                if rd_valid = '1' then -- packet data available!
                    if rd_dout(17 downto 16) = "00" then
                        if address_valid = '1' then
                            mem_addr <= start_addr;
                            state <= copy;
                        else                            
                            alloc_req <= '1';
                            state <= get_block;
                        end if;
                    else
                        state <= drop; -- resyncronize
                    end if;
                end if;
            
            when get_block =>
                if alloc_resp.done = '1' then
                    alloc_req <= '0';
                    block_id <= alloc_resp.id;
                    if alloc_resp.error = '1' then
                        state <= drop;
                    else
                        start_addr <= alloc_resp.address;
                        mem_addr <= alloc_resp.address;
                        address_valid <= '1';
                        state <= copy;
                    end if;                    
                end if;
            
            when drop =>
                if rd_valid = '1' and rd_dout(17 downto 16) /= "00" then
                    state <= idle;
                end if;

            when copy =>
                if rd_valid = '1' then
                    if mac_idx /= 3 then
                        if rd_dout(15 downto 0) /= X"FFFF" and rd_dout(15 downto 0) /= my_mac_16(mac_idx) then
                            for_me <= '0';
                        end if;
                        mac_idx <= mac_idx + 1;
                    end if;                        

                    toggle <= not toggle;
                    if toggle = '0' then
                        mem_data(31 downto 16) <= rd_dout(15 downto 0);
                        write_req <= '1';
                        state <= ram_write;
                    else
                        mem_data(15 downto 0) <= rd_dout(15 downto 0);
                    end if;                    

                    case rd_dout(17 downto 16) is
                    when "01" =>
                        -- overflow detected
                        write_req <= '0';
                        state <= idle;
                    when "10" =>
                        -- packet with bad CRC
                        write_req <= '0';
                        state <= idle;
                    when "11" =>
                        -- correct packet!
                        used_req.bytes <= unsigned(rd_dout(used_req.bytes'range)) - 5; -- snoop FF and CRC
                        write_req <= '1';
                        state <= valid_packet;
                    when others =>
                        null;
                    end case;
                end if;

            when ram_write =>
                if mem_resp.rack_tag = g_mem_tag then
                    write_req <= '0';
                    mem_addr <= mem_addr + 4;
                    state <= copy;
                end if;                

            when valid_packet =>
                if mem_resp.rack_tag = g_mem_tag then
                    write_req <= '0';
                    if for_me = '1' or promiscuous = '1' then
                        address_valid <= '0';
                        used_req.request <= '1';
                        used_req.id <= block_id;
                        state <= pushing;
                    else
                        state <= idle;
                    end if;
                end if;

            when pushing =>
                if used_resp = '1' then
                    used_req.request <= '0';
                    state <= idle;
                end if;
                
            end case;

            if reset = '1' then
                alloc_req <= '0';
                used_req <= c_used_req_init;
                state <= idle;
                address_valid <= '0';
                write_req <= '0';
            end if;
        end if;
    end process;

    mem_req.request <= write_req;
    mem_req.data    <= mem_data;
    mem_req.address <= mem_addr;
    mem_req.read_writen <= '0';
    mem_req.byte_en <= (others => '1');
    mem_req.tag <= g_mem_tag;

    process(state)
    begin
        case state is
        when drop =>
            rd_next <= '1';
        when copy =>
            rd_next <= '1'; -- do something here with memory
        when others =>
            rd_next <= '0';
        end case;        
    end process;
    
end architecture;
