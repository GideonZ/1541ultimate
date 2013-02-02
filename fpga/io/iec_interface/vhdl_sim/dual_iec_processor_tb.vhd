library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;


library work;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.tl_string_util_pkg.all;

entity dual_iec_processor_tb is

end dual_iec_processor_tb;

architecture tb of dual_iec_processor_tb is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal master_req      : t_io_req;
    signal master_resp     : t_io_resp;
    signal slave_req       : t_io_req;
    signal slave_resp      : t_io_resp;
    signal slave_clk_o     : std_logic;
    signal slave_data_o    : std_logic;
    signal slave_atn_o     : std_logic;
    signal srq_o           : std_logic;
    signal master_clk_o    : std_logic;
    signal master_data_o   : std_logic;
    signal master_atn_o    : std_logic;
    signal iec_clock       : std_logic;
    signal iec_data        : std_logic;
    signal iec_atn         : std_logic;
    signal received        : std_logic_vector(7 downto 0);
    signal eoi             : std_logic;    
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_master: entity work.iec_processor_io
    port map (
        clock           => clock,
        reset           => reset,
        req             => master_req,
        resp            => master_resp,
        clk_o           => master_clk_o,
        clk_i           => iec_clock,
        data_o          => master_data_o,
        data_i          => iec_data,
        atn_o           => master_atn_o,
        atn_i           => iec_atn,
        srq_o           => srq_o,
        srq_i           => srq_o );

    i_slave: entity work.iec_processor_io
    port map (
        clock           => clock,
        reset           => reset,
        req             => slave_req,
        resp            => slave_resp,
        clk_o           => slave_clk_o,
        clk_i           => iec_clock,
        data_o          => slave_data_o,
        data_i          => iec_data,
        atn_o           => slave_atn_o,
        atn_i           => iec_atn,
        srq_o           => open,
        srq_i           => srq_o );
        
    iec_clock <= '0' when (slave_clk_o='0')  or (master_clk_o='0') else 'H';
    iec_data  <= '0' when (slave_data_o='0') or (master_data_o='0') else 'H';
    iec_atn   <= '0' when (slave_atn_o='0')  or (master_atn_o='0') else 'H';    

    i_io_bfm1: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm_master" )
    port map (
        clock       => clock,
    
        req         => master_req,
        resp        => master_resp );

    i_io_bfm2: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm_slave" )
    port map (
        clock       => clock,
    
        req         => slave_req,
        resp        => slave_resp );

    process
        variable iom  : p_io_bus_bfm_object;
        variable stat : std_logic_vector(7 downto 0);
        variable data : std_logic_vector(7 downto 0);
    begin
        wait for 1 us;
        bind_io_bus_bfm("io_bfm_master", iom);
        
        io_write(iom, X"3", X"01"); -- enable master

        io_write(iom, X"5", X"4D"); -- switch to master mode
        io_write(iom, X"4", X"2A"); -- listen 10
        io_write(iom, X"4", X"F0"); -- open file on channel 0
        io_write(iom, X"5", X"4C"); -- attention to TX
        io_write(iom, X"4", X"41"); -- 'A'
        io_write(iom, X"5", X"01"); -- EOI
        io_write(iom, X"4", X"42"); -- 'B'
        io_write(iom, X"5", X"4D"); -- again, be master
        io_write(iom, X"4", X"3F"); -- unlisten
        io_write(iom, X"4", X"4A"); -- talk #10!
        io_write(iom, X"4", X"60"); -- give data from channel 0
        io_write(iom, X"5", X"4A"); -- attention to RX turnaround
        
        wait;
    end process;

    process
        variable ios  : p_io_bus_bfm_object;
        variable stat : std_logic_vector(7 downto 0);
        variable data : std_logic_vector(7 downto 0);
    begin
        wait for 1 us;
        bind_io_bus_bfm("io_bfm_slave", ios);
        
        io_write(ios, X"3", X"01"); -- enable slave

        while true loop
            io_read(ios, X"2", stat);
            if stat(0)='0' then -- byte available
                io_read(ios, X"6", data);
                if stat(7)='1' then -- control byte
                    report "Control byte received: " & hstr(data);
                    if (data = X"43") then
                        report "Talk back!";
                        io_write(ios, X"4", X"31");
                        io_write(ios, X"4", X"32");
                        io_write(ios, X"4", X"33");
                        io_write(ios, X"5", X"01"); -- EOI
                        io_write(ios, X"4", X"34");
                    end if;
                else
                    report "Data byte received: " & hstr(data);
                end if;    
            end if;
        end loop;        
        wait;
    end process;


end architecture;
 