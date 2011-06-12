-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- 
-- Author: Gideon Zweijtzer (gideon.zweijtzer (at) gmail.com)
--
-- Note that this file is copyrighted, and is not supposed to be used in other
-- projects without written permission from the author.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity sid_trace is
generic (
    g_mem_tag   : std_logic_vector(7 downto 0) := X"CE" );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    addr        : in  unsigned(6 downto 0);
    wren        : in  std_logic;
    wdata       : in  std_logic_vector(7 downto 0);

    phi2_tick   : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

    mem_req     : out t_mem_req;
    mem_resp    : in  t_mem_resp );
end entity;    

architecture gideon of sid_trace is
    type t_reg_bank is array(natural range <>) of std_logic_vector(7 downto 0);
    signal reg_start    : t_reg_bank(0 to 31) := (others => (others => '0'));
    type t_state is (idle, trace, finish);
    signal state        : t_state;
    type t_mem_state is (idle, do_req, wait_ack );
    signal mem_state    : t_mem_state;
    signal address      : unsigned(25 downto 0);
    signal count        : unsigned(23 downto 0);
    signal fifo_din     : std_logic_vector(38 downto 0);
    signal fifo_dout    : std_logic_vector(38 downto 0);
    signal write_data   : std_logic_vector(39 downto 0);
    signal fifo_valid   : std_logic;
    signal fifo_pop     : std_logic;
    signal fifo_push    : std_logic;
    signal byte_count   : integer range 0 to 7;
    signal triggered    : std_logic;
    signal memory_full  : std_logic;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                count <= (others => '0');
                if wren='1' then
                    reg_start(to_integer(addr(4 downto 0))) <= wdata;
                end if;
                if wren='1' and triggered='1' then
                    triggered <= '0';
                    state <= trace;
                end if;
            when trace =>
                if wren='1' then
                    count <= to_unsigned(1, count'length);
                elsif phi2_tick='1' then
                    count <= count + 1;
                end if;
            when finish =>
                null;
            when others =>
                null;
            end case;

            io_resp <= c_io_resp_init;

            if io_req.read='1' then
                io_resp.ack <= '1';
                if io_req.address(7)='0' then
                    io_resp.data <= reg_start(to_integer(io_req.address(4 downto 0)));
                else
                    case io_req.address(1 downto 0) is
                    when "00" =>
                        io_resp.data <= std_logic_vector(address(7 downto 0));
                    when "01" =>
                        io_resp.data <= std_logic_vector(address(15 downto 8));
                    when "10" =>
                        io_resp.data <= std_logic_vector(address(23 downto 16));
                    when "11" =>
                        io_resp.data <= "000000" & std_logic_vector(address(25 downto 24));
                    when others =>
                        null;
                    end case;
                end if;
            elsif io_req.write='1' then
                io_resp.ack <= '1';
                if io_req.data = X"33" then
                    triggered <= '1';
                elsif io_req.data = X"44" then
                    state <= finish;
                elsif io_req.data = X"55" then
                    state <= idle;
                end if;
            end if;

            if reset='1' then
                triggered <= '0';
                state <= idle;
            end if;                    
        end if;
    end process;
    
    fifo_din   <= std_logic_vector(addr) & wdata & std_logic_vector(count);
    fifo_push  <= '1' when wren='1' and state = trace else '0';
    write_data <= '0' & fifo_dout;
    
    i_fifo: entity work.SRL_fifo
    generic map ( Width => 39 )
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => fifo_pop,
        PutElement  => fifo_push,
        FlushFifo   => '0',
        DataIn      => fifo_din,
        DataOut     => fifo_dout,
        SpaceInFifo => open,
        AlmostFull  => open,
        DataInFifo  => fifo_valid );

    process(clock)
    begin
        if rising_edge(clock) then
            fifo_pop <= '0';
            case mem_state is
            when idle =>
                if fifo_valid='1' and fifo_pop='0' and memory_full='0' then
                    byte_count <= 4;
                    mem_state <= do_req;
                end if;
            
            when do_req =>
                mem_req.request <= '1';
                mem_req.data <= write_data(byte_count*8+7 downto byte_count*8);
                mem_state <= wait_ack;
           
            when wait_ack =>
                if mem_resp.rack='1' and mem_resp.rack_tag=g_mem_tag then
                    mem_req.request <= '0';
                    address <= address + 1;
                    if address = 33554431 then
                        memory_full <= '1';
                    end if;
                    if byte_count = 0 then
                        fifo_pop <= '1';
                        mem_state <= idle;
                    else
                        byte_count <= byte_count -1;
                        mem_state <= do_req;
                    end if;
                end if;
            
            when others =>
                null;
            end case;

            if reset='1' then
                memory_full <= '0';
                mem_req.data <= (others => '0');
                mem_req.request <= '0';
                mem_state <= idle;
                address <= to_unsigned(16777216, address'length);
            end if;                    
        end if;
    end process;

    mem_req.address     <= address;
    mem_req.read_writen <= '0';
    mem_req.tag         <= g_mem_tag;
end gideon;

