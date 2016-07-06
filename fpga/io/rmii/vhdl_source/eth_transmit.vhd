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
    io_irq          : out std_logic;
    
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

    type t_state is (idle, wait_mem, copy);
    signal state    : t_state;

    signal trigger          : std_logic;
    signal trigger_sent     : std_logic;
    signal wr_en            : std_logic;
    signal wr_din           : std_logic_vector(8 downto 0) := (others => '0');
    signal wr_full          : std_logic := '0';

    signal busy             : std_logic;
    signal start            : std_logic;
    signal done             : std_logic;
        
    -- memory signals
    signal sw_addr      : std_logic_vector(25 downto 0);
    signal sw_length    : std_logic_vector(11 downto 0);
    signal down_count   : unsigned(11 downto 0);
    signal up_count     : unsigned(11 downto 0);
    signal offset       : unsigned(1 downto 0) := (others => '0');
    signal mem_addr     : unsigned(25 downto 2) := (others => '0');
    signal mem_data     : std_logic_vector(31 downto 0);
    signal read_req     : std_logic;

    -- signals in eth_clock domain
    signal eth_rd_next  : std_logic;
    signal eth_rd_dout  : std_logic_vector(8 downto 0);
    signal eth_rd_valid : std_logic;
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
                    sw_addr(7 downto 0) <= io_req.data;
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

            if done = '1' then
                busy <= '0';
                io_irq <= '1';
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

    
    process(clock)
    begin
        if rising_edge(clock) then
            trigger <= '0';
            done <= '0';
            case state is
            when idle =>
                if wr_full = '0' then
                    wr_en <= '0';
                end if;
                mem_addr <= unsigned(sw_addr(25 downto 2));
                down_count <= unsigned(sw_length);
                up_count <= (others => '0');
                offset <= unsigned(sw_addr(1 downto 0));
                trigger_sent <= '0';

                if start = '1' then
                    read_req <= '1';
                    state <= wait_mem;
                end if;
                
            when wait_mem =>
                if wr_full = '0' then
                    wr_en <= '0';
                end if;
                if mem_resp.rack = '1' and mem_resp.rack_tag = g_mem_tag then
                    read_req <= '0';
                    mem_addr <= mem_addr + 1;
                end if;
                if mem_resp.dack_tag = g_mem_tag then
                    mem_data <= mem_resp.data;
                    state <= copy;
                end if;

            when copy =>
                if wr_full = '0' then
                    wr_din(7 downto 0) <= mem_data(7+8*to_integer(offset) downto 8*to_integer(offset));
                    wr_en <= '1';
    
                    if offset = 3 then
                        read_req <= '1';
                        state <= wait_mem;
                    end if;
                    offset <= offset + 1;
    
                    if down_count = 1 then
                        wr_din(8) <= '1';
                        read_req <= '0';
                        done <= '1';
                        state <= idle;
                        trigger <= not trigger_sent;
                    else
                        wr_din(8) <= '0';
                    end if; 
    
                    if up_count = 512 then
                        trigger <= '1';
                        trigger_sent <= '1';
                    end if;
    
                    down_count <= down_count - 1;
                    up_count <= up_count + 1;                
                end if;
            end case;             

            if reset = '1' then
                read_req <= '0';
                state <= idle;
            end if;
        end if;
    end process;

    mem_req.request <= read_req;
    mem_req.data    <= mem_data;
    mem_req.address <= mem_addr & "00";
    mem_req.read_writen <= '1';
    mem_req.byte_en <= (others => '1');
    mem_req.tag     <= g_mem_tag;
    
end architecture;
