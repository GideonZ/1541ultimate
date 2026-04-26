-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : eth_filter
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
        
entity eth_filter is
generic (
    g_max_packet    : natural := 1536;
    g_mem_tag       : std_logic_vector(7 downto 0) := X"13" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    -- io interface for local cpu
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;

    -- interface to free block system
    alloc_req       : out std_logic;
    alloc_resp      : in  t_alloc_resp;
    used_req        : out t_used_req;
    used_resp       : in  std_logic;

    -- interface to memory
    mem_req         : out t_mem_req_32;
    mem_resp        : in  t_mem_resp_32;

    ----
    eth_clock       : in std_logic;
    eth_reset       : in std_logic;

    eth_rx_data     : in std_logic_vector(7 downto 0);
    eth_rx_sof      : in std_logic;
    eth_rx_eof      : in std_logic;
    eth_rx_valid    : in std_logic );


end entity;


architecture gideon of eth_filter is
    -- signals in the sys_clock domain
    type t_mac is array(0 to 5) of std_logic_vector(7 downto 0);
    signal my_mac   : t_mac := (others => (others => '0'));
    type t_mac_16 is array(0 to 2) of std_logic_vector(15 downto 0);
    signal my_mac_16 : t_mac_16 := (others => (others => '0'));

    signal eth_rx_enable : std_logic;

    signal clear        : std_logic;
    signal rx_enable    : std_logic;
    signal promiscuous  : std_logic;
    signal rd_next      : std_logic;
    signal rd_dout      : std_logic_vector(17 downto 0);
    signal rd_valid     : std_logic;
    
    type t_receive_state is (idle, odd, even, len, ovfl);
    signal receive_state    : t_receive_state;
    
    type t_state is (idle, get_block, drop, copy, valid_packet, pushing, ram_write);
    signal state    : t_state;
    signal address_valid    : std_logic;
    signal start_addr       : unsigned(25 downto 0);
    signal mac_idx          : integer range 0 to 3;
    signal for_me           : std_logic;
    signal block_id         : unsigned(7 downto 0);
    
    -- memory signals
    signal mem_addr     : unsigned(25 downto 0) := (others => '0');
    signal mem_data     : std_logic_vector(31 downto 0) := (others => '0');
    signal write_req    : std_logic;
    
    -- signals in eth_clock domain
    signal eth_wr_din   : std_logic_vector(17 downto 0);
    signal eth_wr_en    : std_logic;
    signal eth_wr_full  : std_logic;
    signal eth_length   : unsigned(10 downto 0);
    -- round down to the last odd value smaller than given max length
    -- 1536 /2 = 768 -1 = 767 = 10.1111.1111 & 1 => 101.1111.1111 = 1535
    constant c_eth_maxlen : unsigned(10 downto 0) := to_unsigned((g_max_packet/2)-1, 10) & '1';
    signal crc_ok       : std_logic;
    signal toggle       : std_logic;
begin
    -- 18 wide fifo; 16 data bits and 2 control bits
    -- 00 = packet data
    -- 01 = overflow detected => drop
    -- 10 = packet with CRC error => drop
    -- 11 = packet OK!

    i_fifo: entity work.async_fifo_ft
    generic map (
        --g_fast       => true,
        g_data_width => 18,
        g_depth_bits => 9
    )
    port map (
        wr_clock     => eth_clock,
        wr_reset     => eth_reset,
        wr_en        => eth_wr_en,
        wr_din       => eth_wr_din,
        wr_full      => eth_wr_full,

        rd_clock     => clock,
        rd_reset     => clear,
        rd_next      => rd_next,
        rd_dout      => rd_dout,
        rd_valid     => rd_valid
    );

    clear <= reset or not rx_enable;
    
    process(eth_clock)
    begin
        if rising_edge(eth_clock) then
            eth_wr_en <= '0';
            
            case receive_state is
            when idle =>
                eth_wr_din(7 downto 0) <= eth_rx_data;
                eth_length <= to_unsigned(1, eth_length'length);
                if eth_rx_sof = '1' and eth_rx_valid = '1' then
                    receive_state <= odd;
                end if;
            
            when odd =>
                eth_wr_din(15 downto 8) <= eth_rx_data;
                eth_wr_din(17 downto 16) <= "00";

                if eth_rx_valid = '1' then
                    eth_length <= eth_length + 1;
                    if eth_length = c_eth_maxlen or eth_wr_full = '1' then
                        receive_state <= ovfl;
                    else
                        eth_wr_en <= '1';
                        crc_ok <= eth_rx_data(0);
                        if eth_rx_eof = '1' then
                            receive_state <= len;
                        else                        
                            receive_state <= even;
                        end if;
                    end if;
                end if;

            when even =>
                eth_wr_din(7 downto 0) <= eth_rx_data;
                eth_wr_din(17 downto 16) <= "00";

                if eth_rx_valid = '1' then
                    eth_length <= eth_length + 1;
                    if eth_rx_eof = '1' then
                        receive_state <= len;
                        crc_ok <= eth_rx_data(0);
                    else
                        receive_state <= odd;
                    end if;
                end if;

            when len =>
                if eth_wr_full = '0' then
                    eth_wr_din <= (others => '0');
                    eth_wr_din(17) <= '1';
                    eth_wr_din(16) <= crc_ok;
                    eth_wr_din(eth_length'range) <= std_logic_vector(eth_length);
                    eth_wr_en <= '1';
                    receive_state <= idle;                                        
                else
                    receive_state <= ovfl;                                        
                end if;
            
            when ovfl =>
                if eth_wr_full = '0' then
                    eth_wr_din(17 downto 16) <= "01";
                    eth_wr_en <= '1';
                    receive_state <= idle;
                end if;

            end case;

            if eth_reset = '1' or eth_rx_enable = '0' then
                receive_state <= idle;
                crc_ok <= '0';
            end if;                
        end if;
    end process;

    i_sync_enable: entity work.level_synchronizer
    port map (
        clock       => eth_clock,
        reset       => eth_reset,
        input       => rx_enable,
        input_c     => eth_rx_enable
    );

    process(clock)
        alias local_addr : unsigned(3 downto 0) is io_req.address(3 downto 0);
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;

            if io_req.write = '1' then
                io_resp.ack <= '1';
                case local_addr is
                when X"0"|X"1"|X"2"|X"3"|X"4"|X"5" =>
                    my_mac(to_integer(local_addr)) <= io_req.data;

                when X"7" =>
                    promiscuous <= io_req.data(0);
                    
                when X"8" =>
                    rx_enable <= io_req.data(0);
                    
                when others =>
                    null; 
                end case;
            end if;

            if io_req.read = '1' then
                io_resp.ack <= '1';
                case local_addr is
                when others =>
                    null; 
                end case;
            end if;

            if reset = '1' then
                promiscuous <= '0';
                rx_enable <= '0';
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
                        if for_me = '1' or promiscuous = '1' then
                            write_req <= '1';
                            state <= ram_write;
                        end if;
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
                        
                        if for_me = '1' or promiscuous = '1' then
                            write_req <= '1';
                            state <= valid_packet;
                        else
                            state <= idle;
                        end if;
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

            if reset = '1' or rx_enable = '0' then
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
