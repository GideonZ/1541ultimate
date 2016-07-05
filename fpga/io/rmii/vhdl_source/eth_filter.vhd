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
    
entity eth_filter is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    -- io interface for local cpu
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;

    -- interface to free block system
    block_req       : out t_block_req;
    block_resp      : in  t_block_resp;

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

    signal promiscuous  : std_logic;
    signal rd_next      : std_logic;
    signal rd_dout      : std_logic_vector(17 downto 0);
    signal rd_valid     : std_logic;

    type t_state is (idle, get_block, drop, copy, valid_packet, pushing);
    signal state    : t_state;
    signal address_valid    : std_logic;
    signal start_addr       : unsigned(25 downto 0);
    signal length           : unsigned(11 downto 0);    
    signal mac_idx          : integer range 0 to 3;
    signal for_me           : std_logic;
    
    -- signals in eth_clock domain
    signal rx_data_d    : std_logic_vector(7 downto 0) := (others => '0');
    signal eth_wr_din   : std_logic_vector(17 downto 0);
    signal eth_wr_en    : std_logic;
    signal eth_wr_full  : std_logic;
    signal toggle       : std_logic;
    signal overflow     : std_logic;
begin
    -- 18 wide fifo; 16 data bits and 2 control bits
    -- 00 = packet data
    -- 01 = end; one valid byte
    -- 10 = end; two valid bytes
    -- 11 = drop; end of packet, overflow detected, or CRC error

    i_fifo: entity work.async_fifo_ft
    generic map (
        g_fast       => true,
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
        rd_reset     => reset,
        rd_next      => rd_next,
        rd_dout      => rd_dout,
        rd_valid     => rd_valid
    );

    process(eth_clock)
    begin
        if rising_edge(eth_clock) then
            if eth_rx_valid = '1' then
                rx_data_d <= eth_rx_data;
                eth_wr_din <= "00" & eth_rx_data & rx_data_d;
                
                eth_wr_en <= '0';
                if eth_rx_sof = '1' then -- first byte of new packet
                    overflow <= '0';
                    toggle <= '0';
                else
                    toggle <= not toggle;
                    if toggle = '0' then -- 2 bytes received
                        eth_wr_en <= not overflow;
                    end if;
                end if;
                
                if eth_wr_full = '1' then
                    overflow <= '1';
                end if;
                
                if eth_rx_eof = '1' then
                    eth_wr_en <= '1';
                    if overflow = '1' or eth_rx_data /= X"FF" then
                        eth_wr_din(17 downto 16) <= "11";
                    else
                        eth_wr_din(17) <= toggle;
                        eth_wr_din(16) <= not toggle;
                    end if;
                end if;
            end if;

            if eth_reset = '1' then
                toggle <= '0';
                overflow <= '0';
            end if;                
        end if;
    end process;


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
                null;
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
                for_me <= '1';
                length <= (others => '0');
                if rd_valid = '1' then -- packet data available!
                    if rd_dout(17 downto 16) = "00" then
                        if address_valid = '1' then
                            state <= copy;
                        else                            
                            block_req.request <= '1';
                            block_req.command <= allocate;
                            state <= get_block;
                        end if;
                    else
                        state <= drop; -- resyncronize
                    end if;
                end if;
            
            when get_block =>
                if block_resp.done = '1' then
                    block_req.request <= '0';
                    block_req.id <= block_resp.id;
                    if block_resp.error = '1' then
                        state <= drop;
                    else
                        start_addr <= block_resp.address;
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

                    case rd_dout(17 downto 16) is
                    when "00" =>
                        length <= length + 2;
                    when "01" =>
                        length <= length + 1;
                        state <= valid_packet;
                    when "10" =>
                        length <= length + 2;
                        state <= valid_packet;
                    when "11" =>
                        state <= idle;
                    when others =>
                        null;
                    end case;
                end if;

            when valid_packet =>
                if for_me = '1' or promiscuous = '1' then
                    address_valid <= '0';
                    block_req.request <= '1';
                    block_req.command <= write;
                    block_req.bytes   <= length;
                    state <= pushing;
                else
                    state <= idle;
                end if;

            when pushing =>
                if block_resp.done = '1' then
                    block_req.request <= '0';
                    if block_resp.error = '1' then
                        state <= valid_packet;                        
                    else
                        state <= idle;
                    end if;
                end if;
                
            end case;

            if reset = '1' then
                block_req <= c_block_req_init;
                state <= idle;
                address_valid <= '0';
            end if;
        end if;
    end process;

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
