--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: This block implements a simple DMA controller for reading a 
--              byte stream from memory. Note that there is no buffering, so
--              this is not super efficient. But it is intended to be used for
--              uart communication. Even at 6.67 Mbps, a byte takes 1.5 us,
--              so one memory transfer occurs every 6 us, and as long as the
--              latency is < 1.5us, no hickups are expected. 
--------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
        
entity rx_dma is
generic (
    g_mem_tag       : std_logic_vector(7 downto 0) := X"14" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    -- AXI Address Stream (in)
    addr_data       : in  std_logic_vector(27 downto 0);
    addr_valid      : in  std_logic;
    addr_ready      : out std_logic;

    -- AXI Length Stream (out)
    len_data        : out std_logic_vector(15 downto 0);
    len_valid       : out std_logic;
    len_ready       : in  std_logic;

    -- interface to memory (write)
    mem_req         : out t_mem_req_32;
    mem_resp        : in  t_mem_resp_32;

    -- AXI byte stream to be written
    in_data         : in  std_logic_vector(7 downto 0) := X"00";
    in_valid        : in  std_logic;
    in_last         : in  std_logic;
    in_ready        : out std_logic
);
end entity;

architecture gideon of rx_dma is

    type t_state is (idle, collect, write_mem, write_last, report_len);
    signal state    : t_state;

    signal mem_addr     : unsigned(25 downto 0) := (others => '0');
    signal mem_data     : std_logic_vector(31 downto 0);
    signal mem_be       : std_logic_vector(3 downto 0);
    signal write_req    : std_logic;
    signal bytecount    : unsigned(15 downto 0);
    signal len_valid_i  : std_logic;
begin
    len_valid <= len_valid_i;
    in_ready  <= '1' when state = collect else '0';

    process(clock)
    begin
        if rising_edge(clock) then
            if len_ready = '1' then
                len_valid_i <= '0';
            end if;

            addr_ready <= '0';
            case state is
            when idle =>
                bytecount <= (others => '0');
                mem_be <= "0000";
                write_req <= '0';
                if addr_valid = '1' then
                    addr_ready <= '1';
                    mem_addr <= unsigned(addr_data(mem_addr'range));
                    state <= collect;
                end if;

            when collect =>
                if in_valid = '1' then
                    for i in 0 to 3 loop
                        if mem_addr(1 downto 0) = i then
                            mem_data(7+8*i downto 8*i) <= in_data;
                            mem_be(i) <= '1';
                        end if;
                    end loop;
                    bytecount <= bytecount + 1;
                    if in_last = '1' then
                        write_req <= '1';
                        state <= write_last;
                    elsif mem_addr(1 downto 0) = "11" then
                        write_req <= '1';
                        state <= write_mem;
                    else
                        mem_addr <= mem_addr + 1;
                    end if;
                end if;

            when write_mem =>
                if mem_resp.rack = '1' and mem_resp.rack_tag = g_mem_tag then
                    mem_be <= "0000";
                    write_req <= '0';
                    mem_addr <= mem_addr + 1;
                    state <= collect;
                end if;

            when write_last =>
                if mem_resp.rack = '1' and mem_resp.rack_tag = g_mem_tag then
                    mem_be <= "0000";
                    write_req <= '0';
                    state <= report_len;
                end if;

            when report_len =>
                if len_ready = '1' or len_valid_i = '0' then
                    len_data <= std_logic_vector(bytecount);
                    len_valid_i <= '1';
                    state <= idle;
                end if;

            end case;             

            if reset = '1' then
                len_valid_i <= '0';
                write_req <= '0';
                state <= idle;
            end if;
        end if;
    end process;

    mem_req.request <= write_req;
    mem_req.data    <= mem_data;
    mem_req.address <= mem_addr(mem_addr'high downto 2) & "00";
    mem_req.read_writen <= '0';
    mem_req.byte_en <= mem_be;
    mem_req.tag     <= g_mem_tag;
    
end architecture;
