-------------------------------------------------------------------------------
-- Title      : eth_filter_tb
-- Author     : Gideon Zweijtzer (gideon.zweijtzer@gmail.com)
-------------------------------------------------------------------------------
-- Description: Testbench for eth_filter
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
   
library work;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.block_bus_pkg.all;
use work.mem_bus_pkg.all;

entity eth_filter_tb is

end entity;

architecture testcase of eth_filter_tb is
    signal clock               : std_logic := '0'; -- 50 MHz reference clock
    signal reset               : std_logic;
    signal io_clock            : std_logic := '0'; -- 66 MHz reference clock
    signal io_reset            : std_logic;
    signal rmii_crs_dv         : std_logic;
    signal rmii_rxd            : std_logic_vector(1 downto 0);
    signal rmii_tx_en          : std_logic;
    signal rmii_txd            : std_logic_vector(1 downto 0);
    signal eth_rx_data         : std_logic_vector(7 downto 0);
    signal eth_rx_sof          : std_logic;
    signal eth_rx_eof          : std_logic;
    signal eth_rx_valid        : std_logic;
    signal eth_tx_data         : std_logic_vector(7 downto 0);
    signal eth_tx_sof          : std_logic;
    signal eth_tx_eof          : std_logic;
    signal eth_tx_valid        : std_logic;
    signal eth_tx_ready        : std_logic;
    signal ten_meg_mode        : std_logic;

    signal io_req          : t_io_req;
    signal io_resp         : t_io_resp;
    signal io_req_free     : t_io_req;
    signal io_resp_free    : t_io_resp;
    signal io_req_filt     : t_io_req;
    signal io_resp_filt    : t_io_resp;
    signal io_irq          : std_logic;

    signal alloc_req       : std_logic := '0';
    signal alloc_resp      : t_alloc_resp;
    signal used_req        : t_used_req;
    signal used_resp       : std_logic;
    signal mem_req         : t_mem_req_32;
    signal mem_resp        : t_mem_resp_32 := c_mem_resp_32_init;
    
    type t_byte_array is array (natural range <>) of std_logic_vector(7 downto 0);

    constant c_frame_with_crc : t_byte_array := (
        X"00", X"0A", X"E6", X"F0", X"05", X"A3", X"00", X"12", X"34", X"56", X"78", X"90", X"08", X"00", X"45", X"00",
        X"00", X"30", X"B3", X"FE", X"00", X"00", X"80", X"11", X"72", X"BA", X"0A", X"00", X"00", X"03", X"0A", X"00",
        X"00", X"02", X"04", X"00", X"04", X"00", X"00", X"1C", X"89", X"4D", X"00", X"01", X"02", X"03", X"04", X"05",
        X"06", X"07", X"08", X"09", X"0A", X"0B", X"0C", X"0D", X"0E", X"0F", X"10", X"11", X"12", X"13"
--        , X"7A", X"D5", X"6B", X"B3"
        );

    procedure io_write_long(variable io : inout p_io_bus_bfm_object; addr : natural; data : std_logic_vector(31 downto 0)) is
    begin
        io_write(io, to_unsigned(addr+0, 20), data(07 downto 00));
        io_write(io, to_unsigned(addr+1, 20), data(15 downto 08));
        io_write(io, to_unsigned(addr+2, 20), data(23 downto 16));
        io_write(io, to_unsigned(addr+3, 20), data(31 downto 24));
    end procedure;

    procedure io_write(variable io : inout p_io_bus_bfm_object; addr : natural; data : std_logic_vector(7 downto 0)) is
    begin
        io_write(io, to_unsigned(addr, 20), data);
    end procedure;
    procedure io_read(variable io : inout p_io_bus_bfm_object; addr : natural; data : out std_logic_vector(7 downto 0)) is
    begin
        io_read(io, to_unsigned(addr, 20), data);
    end procedure;
    procedure io_read_word(variable io : inout p_io_bus_bfm_object; addr : natural; data : out std_logic_vector(15 downto 0)) is
    begin
        io_read(io, to_unsigned(addr, 20), data(7 downto 0));
        io_read(io, to_unsigned(addr+1, 20), data(15 downto 8));
    end procedure;

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    io_clock <= not io_clock after 8 ns;
    io_reset <= '1', '0' after 100 ns;

    i_rmii: entity work.rmii_transceiver
    port map (
        clock        => clock,
        reset        => reset,
        rmii_crs_dv  => rmii_crs_dv,
        rmii_rxd     => rmii_rxd,
        rmii_tx_en   => rmii_tx_en,
        rmii_txd     => rmii_txd,
        eth_rx_data  => eth_rx_data,
        eth_rx_sof   => eth_rx_sof,
        eth_rx_eof   => eth_rx_eof,
        eth_rx_valid => eth_rx_valid,
        eth_tx_data  => eth_tx_data,
        eth_tx_sof   => eth_tx_sof,
        eth_tx_eof   => eth_tx_eof,
        eth_tx_valid => eth_tx_valid,
        eth_tx_ready => eth_tx_ready,
        ten_meg_mode => ten_meg_mode
    );
    
    rmii_crs_dv <= rmii_tx_en;
    rmii_rxd    <= rmii_txd;
    
    test: process    
        variable io : p_io_bus_bfm_object;
        variable i : natural;
    begin
        eth_tx_data <= X"00";
        eth_tx_sof  <= '0';
        eth_tx_eof  <= '0';
        eth_tx_valid <= '0';
        ten_meg_mode <= '0';

        wait until reset = '0';
        bind_io_bus_bfm("io", io);
        io_write_long(io, 0, X"01200000");
        -- insert some free blocks
        for x in 1 to 10 loop
            io_write(io, 4, std_logic_vector(to_unsigned(x, 8)));
        end loop;

        -- configure filter
        io_write(io, 256, c_frame_with_crc(0));
        io_write(io, 257, c_frame_with_crc(1));
        io_write(io, 258, c_frame_with_crc(2));
        io_write(io, 259, c_frame_with_crc(3));
        io_write(io, 260, c_frame_with_crc(4));
        io_write(io, 261, c_frame_with_crc(5));


        for x in 0 to 61 loop
            wait until clock = '1';
            i := 0;
            L1: loop
                wait until clock = '1';
                if eth_tx_valid = '0' or eth_tx_ready = '1' then
                    if eth_tx_eof = '1' then
                        eth_tx_eof <= '0';
                        eth_tx_valid <= '0';
                        eth_tx_data <= X"00";
                        exit L1;
                    else                 
                        eth_tx_data <= c_frame_with_crc(i);
                        eth_tx_valid <= '1';
                        if i = c_frame_with_crc'left then
                            eth_tx_sof <= '1';
                        else
                            eth_tx_sof <= '0';
                        end if;
                        if (i+x) = c_frame_with_crc'right then
                            eth_tx_eof <= '1';
                        else
                            eth_tx_eof <= '0';
                        end if;
                        i := i + 1;
                    end if;
                end if;
            end loop;
            if x > 9 then
                -- io_write(io, 4, std_logic_vector(to_unsigned(x+16, 8)));
            end if;
        end loop;
        wait;
    end process;

    i_bfm: entity work.io_bus_bfm
    generic map ("io")
    port map (
        clock  => io_clock,
        req    => io_req,
        resp   => io_resp
    );

    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 8,
        g_range_hi => 9,
        g_ports    => 2
    )
    port map (
        clock      => io_clock,
        req        => io_req,
        resp       => io_resp,
        reqs(1)    => io_req_filt,
        reqs(0)    => io_req_free,
        resps(1)   => io_resp_filt,
        resps(0)   => io_resp_free
    );
    

    i_mut: entity work.eth_filter
    port map (
        clock        => io_clock,
        reset        => io_reset,
        io_req       => io_req_filt,
        io_resp      => io_resp_filt,
        alloc_req    => alloc_req,
        alloc_resp   => alloc_resp,
        used_req     => used_req,
        used_resp    => used_resp,
        mem_req      => mem_req,
        mem_resp     => mem_resp,

        eth_clock    => clock,
        eth_reset    => reset,
        eth_rx_data  => eth_rx_data,
        eth_rx_sof   => eth_rx_sof,
        eth_rx_eof   => eth_rx_eof,
        eth_rx_valid => eth_rx_valid );

    i_free: entity work.free_queue
    port map (
        clock      => io_clock,
        reset      => io_reset,

        alloc_req  => alloc_req,
        alloc_resp => alloc_resp,
        used_req   => used_req,
        used_resp  => used_resp,

        io_req     => io_req_free,
        io_resp    => io_resp_free,
        io_irq     => io_irq
    );

    process(io_clock)
        variable c : integer := 0;
    begin
        if rising_edge(io_clock) then
            if mem_req.request = '1' then
                c := c + 1;
            end if;
            if c = 4 then
                mem_resp.rack <= '1';
                mem_resp.rack_tag <= mem_req.tag;
                c := 0;
            else
                mem_resp.rack <= '0';
                mem_resp.rack_tag <= X"00";
            end if;
        end if;
    end process;

    process
        variable io : p_io_bus_bfm_object;
        variable data : std_logic_vector(7 downto 0);
        variable id : std_logic_vector(7 downto 0);
        variable data16 : std_logic_vector(15 downto 0);
    begin
        wait until io_reset = '0';
        bind_io_bus_bfm("io", io);
        while true loop        
            if io_irq /= '1' then
                wait until io_irq = '1';
            end if;
            io_read(io, 15, data);
            assert data(0) = '1' report "Expected an allocated block to be available";
            io_read(io, 8, id);
            io_read_word(io, 10, data16);
            io_write(io, 15, X"00"); -- pop!
            io_read(io, 15, data);
            assert data(0) = '0' report "Expected no more allocated blocks to be available";
            io_write(io, 4, id); -- re-add 
        end loop;
    end process;
    


end architecture;
