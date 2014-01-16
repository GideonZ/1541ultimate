library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity nano_tb is
end entity;

architecture tb of nano_tb is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal io_addr     : unsigned(7 downto 0);
    signal io_write    : std_logic;
    signal io_wdata    : std_logic_vector(15 downto 0);
    signal io_rdata    : std_logic_vector(15 downto 0);

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_cpu: entity work.nano
    port map (
        clock       => clock,
        reset       => reset,
        
        -- i/o interface
        io_addr     => io_addr,
        io_write    => io_write,
        io_wdata    => io_wdata,
        io_rdata    => io_rdata );
end architecture;
