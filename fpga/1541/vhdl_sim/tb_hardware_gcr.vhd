library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

entity tb_hardware_gcr is
end tb_hardware_gcr;

architecture tb of tb_hardware_gcr is

    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal cpu_clock_en: std_logic := '1';
    signal cpu_addr    : std_logic_vector(2 downto 0) := "000";
    signal cpu_access  : std_logic := '0';
    signal cpu_write   : std_logic := '0';
    signal cpu_wdata   : std_logic_vector(7 downto 0) := X"00";
    signal cpu_rdata   : std_logic_vector(7 downto 0) := X"00";
    signal busy        : std_logic;
    signal mem_req     : std_logic;
    signal mem_ack     : std_logic := '1';
    signal mem_addr    : std_logic_vector(23 downto 0);
    signal mem_rwn     : std_logic;
    signal mem_wdata   : std_logic_vector(7 downto 0);
    signal mem_rdata   : std_logic_vector(7 downto 0) := X"00";

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;


    hw: entity work.hardware_gcr
    port map (
        clock        => clock,
        reset        => reset,
        
        cpu_clock_en => cpu_clock_en,
        cpu_addr     => cpu_addr,
        cpu_access   => cpu_access,
        cpu_write    => cpu_write,
        
        cpu_wdata    => cpu_wdata,
        cpu_rdata    => cpu_rdata,
        
        busy         => busy,
        ---
        
        mem_req      => mem_req,
        mem_ack      => mem_ack,
        mem_addr     => mem_addr,
        mem_rwn      => mem_rwn,
        mem_wdata    => mem_wdata,
        mem_rdata    => mem_rdata );

    process
        procedure do_write(a: std_logic_vector(2 downto 0);
                         d: std_logic_vector(7 downto 0)) is
        begin
            wait until clock='1';
            cpu_access <= '1';
            cpu_write  <= '1';
            cpu_addr   <= a;
            cpu_wdata  <= d;
            wait until clock='1';
            cpu_access <= '0';
            cpu_write  <= '0';
            for i in 0 to 9 loop
                wait until clock='1';
            end loop;
        end procedure do_write;
    begin
        wait until reset='0';
        do_write("000", X"56");
        do_write("001", X"34");
        do_write("010", X"12");
        do_write("100", X"47");        
        do_write("100", X"47");        
        do_write("100", X"47");        
        do_write("100", X"47");        
        do_write("101", X"00");        
        do_write("101", X"00");        
        do_write("101", X"00");        
        do_write("101", X"00");        
        do_write("101", X"00");        
        do_write("101", X"00");        
        do_write("101", X"00");        
        do_write("100", X"47");        
        do_write("100", X"47");        
        do_write("100", X"47");        
        do_write("100", X"47");        
        wait;        
    end process;
end tb;


