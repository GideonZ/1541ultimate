library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;

entity tb_usb_host is

end entity;

architecture tb of tb_usb_host is
    signal ulpi_clock  : std_logic := '0';
    signal ulpi_reset  : std_logic;
    signal ULPI_DATA   : std_logic_vector(7 downto 0);
    signal ULPI_DIR    : std_logic;
    signal ULPI_NXT    : std_logic;
    signal ULPI_STP    : std_logic;
    
    signal status      : std_logic_vector(7 downto 0) := X"55";
    
    signal sys_clock   : std_logic := '0';
    signal sys_reset   : std_logic;
    signal sys_address : std_logic_vector(12 downto 0) := (others => '0'); -- 8K block
    signal sys_write   : std_logic := '0';
    signal sys_request : std_logic := '0';
    signal sys_wdata   : std_logic_vector(7 downto 0) := X"22";
    signal sys_rdata   : std_logic_vector(7 downto 0);
    signal sys_rack    : std_logic;
    signal sys_dack    : std_logic;

    type t_std_logic_8_vector is array (natural range <>) of std_logic_vector(7 downto 0);

begin
    ulpi_clock <= not ulpi_clock after 8.33 ns;
    ulpi_reset <= '1', '0' after 100 ns;

    sys_clock <= not sys_clock after 10 ns;
    sys_reset <= '1', '0' after 100 ns;

    i_mut: entity work.usb_host
    generic map (
        g_simulation => true )
    port map (
        ulpi_clock  => ulpi_clock,
        ulpi_reset  => ulpi_reset,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP,
        
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
                                  
        sys_address => sys_address, -- 8K block
        sys_write   => sys_write,
        sys_request => sys_request,
        sys_wdata   => sys_wdata,
        sys_rdata   => sys_rdata,
        sys_rack    => sys_rack,
        sys_dack    => sys_dack );

    i_bfm: entity work.ulpi_phy_bfm
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP );

    p_test: process
        procedure write_data(addr : unsigned(15 downto 0);
                             j: t_std_logic_8_vector ) is
            variable a : unsigned(addr'range);                             
        begin
            a := addr;
            for i in j'range loop
                wait until sys_clock='1';
                sys_address <= std_logic_vector(a(12 downto 0));
                sys_write <= '1';
                sys_wdata <= j(i);
                sys_request <= '1';
                a := a + 1;
                -- write cycles can be done every clock cycle
            end loop;
            wait until sys_clock='1';
            sys_write <= '0';
            sys_request <= '0';
        end procedure write_data;

        procedure write_word(addr : unsigned(15 downto 0);
                             j : std_logic_vector(31 downto 0)) is
            variable h : t_std_logic_8_vector(0 to 3);
        begin
            h(0) := j(7 downto 0);
            h(1) := j(15 downto 8);
            h(2) := j(23 downto 16);
            h(3) := j(31 downto 24);
            write_data(addr, h);
        end procedure write_word;
    begin
        wait for 500 ns;

        write_word(X"0000", t_pipe_to_data((
            state               => initialized,
            direction           => dir_out,
            device_address      => (others => '0'),
            device_endpoint     => (others => '0'),
            max_transfer        => to_unsigned(64, 11),
            data_toggle         => '0',
            control             => '1' ) ));

        write_word(X"0004", t_pipe_to_data((
            state               => initialized,
            direction           => dir_in,
            device_address      => (others => '0'),
            device_endpoint     => (others => '0'),
            max_transfer        => to_unsigned(64, 11),
            data_toggle         => '0',
            control             => '1' ) ));

        write_data(X"1000", (X"80", X"06", X"00", X"01", X"00", X"00", X"40", X"00"));

        write_word(X"0100", t_transaction_to_data((
            transaction_type => control,
            state            => busy, -- activate
            pipe_pointer     => "00000",
            transfer_length  => to_unsigned(8, 11),
            buffer_address   => to_unsigned(0, 12) )));

        write_word(X"0104", t_transaction_to_data((
            transaction_type => bulk,
            state            => busy, -- activate
            pipe_pointer     => "00001",
            transfer_length  => to_unsigned(60, 11),
            buffer_address   => to_unsigned(256, 12) )));

        wait for 50 us;
        wait;
    end process;

end tb;
