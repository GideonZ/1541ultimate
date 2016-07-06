-------------------------------------------------------------------------------
-- Title      : eth_transmit_tb
-- Author     : Gideon Zweijtzer (gideon.zweijtzer@gmail.com)
-------------------------------------------------------------------------------
-- Description: Testbench for ethernet transmit unit
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
   
library work;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.block_bus_pkg.all;
use work.mem_bus_pkg.all;

entity eth_transmit_tb is

end entity;

architecture testcase of eth_transmit_tb is
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
    signal ten_meg_mode        : std_logic := '0';

    signal io_req          : t_io_req;
    signal io_resp         : t_io_resp;
    signal io_irq          : std_logic;

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

    procedure io_write_word(variable io : inout p_io_bus_bfm_object; addr : natural; data : std_logic_vector(15 downto 0)) is
    begin
        io_write(io, to_unsigned(addr+0, 20), data(07 downto 00));
        io_write(io, to_unsigned(addr+1, 20), data(15 downto 08));
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
    
    i_mut: entity work.eth_transmit
    generic map (
        g_mem_tag       => X"33"
    )
    port map(
        clock           => io_clock,
        reset           => io_reset,
        io_req          => io_req,
        io_resp         => io_resp,
        io_irq          => io_irq,
        mem_req         => mem_req,
        mem_resp        => mem_resp,
        eth_clock       => clock,
        eth_reset       => reset,
        eth_tx_data     => eth_tx_data,
        eth_tx_sof      => eth_tx_sof,
        eth_tx_eof      => eth_tx_eof,
        eth_tx_valid    => eth_tx_valid,
        eth_tx_ready    => eth_tx_ready
    );


    test: process    
        variable io : p_io_bus_bfm_object;
        variable data : std_logic_vector(7 downto 0);
        --variable i : natural;
    begin
        wait until reset = '0';
        bind_io_bus_bfm("io", io);
        io_write_long(io, 0, X"01200000");
        io_write_word(io, 4, X"0010");
        io_write(io, 8, X"00"); -- start
        wait until io_irq = '1';
        io_write(io, 9, X"00"); -- clear IRQ
        io_write_long(io, 0, X"01200003");
        io_write_word(io, 4, X"0011");
        io_write(io, 8, X"00"); -- start
        wait until io_irq = '1';
        io_write(io, 9, X"00"); -- clear IRQ
        io_write_long(io, 0, X"02200002");
        io_write_word(io, 4, X"0700");
        io_write(io, 8, X"00"); -- start
        wait until io_irq = '1';
        io_write(io, 9, X"00"); -- clear IRQ
        wait;
    end process;

    i_bfm: entity work.io_bus_bfm
    generic map ("io")
    port map (
        clock  => io_clock,
        req    => io_req,
        resp   => io_resp
    );

    process(io_clock)
        variable c : integer := 0;
        variable dack : std_logic_vector(2 downto 0) := "000";
        variable w : natural := 4096;
        impure function get_word return std_logic_vector is
        begin
            w := w + 1;
            return std_logic_vector(to_unsigned(w, 16));
        end function;
    begin
        if rising_edge(io_clock) then
            if mem_req.request = '1' then
                c := c + 1;
            end if;
            dack(2 downto 0) := '0' & dack(2 downto 1);

            if c = 2 then
                mem_resp.rack <= '1';
                mem_resp.rack_tag <= mem_req.tag;
                dack(2) := '1';
                c := 0;
            else
                mem_resp.rack <= '0';
                mem_resp.rack_tag <= X"00";
            end if;
            if dack(0) = '1' then 
                mem_resp.dack_tag <= mem_req.tag;
                mem_resp.data(15 downto 0) <= get_word;
                mem_resp.data(31 downto 16) <= get_word;
            else
                mem_resp.dack_tag <= X"00";
            end if;
        end if;
    end process;

end architecture;
