--------------------------------------------------------------------------------
-- Entity: amd_flash_tb
-- Date:2018-08-14  
-- Author: gideon     
--
-- Description: Testbench for EEPROM
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.tl_string_util_pkg.all;

entity amd_flash_tb is
end entity;

architecture test of amd_flash_tb is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal io_req      : t_io_req;
    signal io_resp     : t_io_resp;

    signal io_irq      : std_logic;
    signal allow_write : std_logic;
    signal address     : unsigned(19 downto 0) := (others => '0');
    signal wdata       : std_logic_vector(7 downto 0) := X"00";
    signal write       : std_logic := '0';
    signal read        : std_logic := '0';
    signal rdata       : std_logic_vector(7 downto 0);
    signal rdata_valid : std_logic;   
    
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_mut: entity work.amd_flash
    port map (
        clock       => clock,
        reset       => reset,
        io_req      => io_req,
        io_resp     => io_resp,
        io_irq      => io_irq,
        allow_write => allow_write,
        address     => address(18 downto 0),
        wdata       => wdata,
        write       => write,
        read        => read,
        rdata       => rdata,
        rdata_valid => rdata_valid
    );
    
    i_bfm: entity work.io_bus_bfm
    generic map(
        g_name => "io"
    )
    port map(
        clock  => clock,
        req    => io_req,
        resp   => io_resp
    );

    p_test: process
        procedure wr(ad : unsigned(19 downto 0); data: std_logic_vector(7 downto 0)) is
        begin
            wait until clock = '1';
            address <= ad;
            wdata <= data;
            wait for 100 ns;
            wait until clock = '1';
            write <= '1';
            wait until clock = '1';
            write <= '0';
        end procedure;

        procedure rd(ad : unsigned(19 downto 0); data: out std_logic_vector(7 downto 0)) is
        begin
            wait until clock = '1';
            address <= ad;
            wdata <= X"FF";
            wait for 100 ns;
            wait until clock = '1';
            data := rdata;
            read <= '1';
            wait until clock = '1';
            read <= '0';
        end procedure;
        variable d : std_logic_vector(7 downto 0);
        variable io : p_io_bus_bfm_object;
    begin
        wait until reset = '0';
        wait for 1 us;
        bind_io_bus_bfm("io", io);
        wr(X"05555", X"AA");
        wr(X"02AAA", X"55");
        wr(X"05555", X"90");
        rd(X"00000", d);
        report "Read: " & hstr(d);
        wr(X"05555", X"AA");
        wr(X"02AAA", X"55");
        wr(X"05555", X"90");
        rd(X"00001", d);
        report "Read: " & hstr(d);

        io_read(io, X"00000", d); 
        assert d = X"00" report "Expected no erase" severity error;
        io_read(io, X"00001", d); 
        assert d = X"00" report "Expected no dirty" severity error;

        wr(X"05555", X"AA");
        wr(X"02AAA", X"55");
        wr(X"05555", X"A0");
        wr(X"01234", X"55");
        io_read(io, X"00000", d); 
        assert d = X"00" report "Expected no erase" severity error;
        io_read(io, X"00001", d); 
        assert d = X"01" report "Expected dirty" severity error;

        wait for 50 ns;
        wr(X"05555", X"AA");
        wr(X"02AAA", X"55");
        wr(X"05555", X"80");
        wr(X"05555", X"AA");
        wr(X"02AAA", X"55");
        wr(X"10000", X"30");

        io_read(io, X"00000", d); 
        assert d = X"02" report "Expected single erase" severity error;

        
        wait for 50 ns;
        rd(X"00000", d);
        rd(X"00000", d);
        rd(X"00000", d);
        rd(X"00000", d);

        io_write(io, X"00000", X"00");
        io_read(io, X"00000", d); 
        assert d = X"00" report "Expected no erase" severity error;

        rd(X"00000", d);
        rd(X"00000", d);

        wait for 50 ns;
        wr(X"05555", X"AA");
        wr(X"02AAA", X"55");
        wr(X"05555", X"80");
        wr(X"05555", X"AA");
        wr(X"02AAA", X"55");
        wr(X"05555", X"10");

        io_read(io, X"00000", d); 
        assert d = X"FF" report "Expected full erase" severity error;

        wait;
    end process;

end architecture;

