library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.itu_pkg.all;

entity itu is
generic (
    g_version	    : unsigned(7 downto 0) := X"FE";
    g_uart          : boolean := true;
    g_frequency     : integer := 50_000_000;
    g_edge_init     : std_logic_vector(7 downto 0) := "00000001";
    g_capabilities  : std_logic_vector(31 downto 0) := X"5555AAAA";
    g_edge_write    : boolean := true;
    g_baudrate      : integer := 115_200;
    g_timer_rate    : integer := 200_000 ); -- 5µs (should not result in more than 8 bits div)
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
                    
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;

    irq_timer_tick  : in  std_logic := '0';

    irq_in          : in  std_logic_vector(7 downto 2);
    irq_out         : out std_logic;
                    
    uart_txd        : out std_logic;
    uart_rxd        : in  std_logic := '1';
    uart_rts        : out std_logic;
    uart_cts        : in  std_logic := '1' );

end itu;

architecture gideon of itu is
    constant c_timer_div : integer := g_frequency / g_timer_rate;
    constant c_baud_div  : integer := g_frequency / g_baudrate;
    signal imask        : std_logic_vector(7 downto 0);
    signal iedge        : std_logic_vector(7 downto 0) := g_edge_init;
    signal timer        : unsigned(7 downto 0);
    signal timer_tick   : std_logic;
    signal timer_div    : integer range 0 to c_timer_div - 1;

    signal irq_timer_val    : unsigned(15 downto 0);
    signal irq_timer_cnt    : unsigned(23 downto 0);
    signal irq_timer_en     : std_logic;
    signal irq_timer_select : std_logic;

    signal irq_en           : std_logic;
    signal irq_c            : std_logic_vector(7 downto 0);
    signal irq_d            : std_logic_vector(7 downto 0);
    signal irq_edge_flag    : std_logic_vector(7 downto 0);
    signal irq_active       : std_logic_vector(7 downto 0);
    
    signal uart_irq         : std_logic := '0';
    
    signal io_req_it        : t_io_req;
    signal io_resp_it       : t_io_resp;
    signal io_req_uart      : t_io_req;
    signal io_resp_uart     : t_io_resp;
begin
    process(clock)
        variable new_irq_edge_flag  : std_logic_vector(irq_edge_flag'range);
    begin
        if rising_edge(clock) then
            if timer_div = 0 then
                timer_div <= c_timer_div - 1;
                timer_tick <= '1';
            else
                timer_div <= timer_div - 1;
                timer_tick <= '0';
            end if;
            
            if timer_tick='1' then
                if timer /= X"00" then
                    timer <= timer - 1;
                end if;
            end if;

            irq_c(7 downto 2) <= irq_in(7 downto 2);
            irq_c(1) <= uart_irq;
            irq_c(0) <= '0';
            if irq_timer_en='1' then
                if irq_timer_cnt = 0 then
                    irq_c(0) <= '1';
                    if irq_timer_select='1' then
                        irq_timer_cnt <= X"00" & irq_timer_val;
                    else
                        irq_timer_cnt <= irq_timer_val & X"FF";
                    end if;
                elsif irq_timer_select='0' or irq_timer_tick='1' then
                    irq_timer_cnt <= irq_timer_cnt - 1;
                end if;
            end if;
            irq_d <= irq_c;

            io_resp_it <= c_io_resp_init;

            new_irq_edge_flag := irq_edge_flag;
            if io_req_it.write='1' then
                io_resp_it.ack <= '1';
                case io_req_it.address(3 downto 0) is
                when c_itu_irq_global =>
                    irq_en <= io_req_it.data(0);
                when c_itu_irq_enable =>
                    imask <= imask or io_req_it.data;
                when c_itu_irq_disable =>
                    imask <= imask and not io_req_it.data;
                when c_itu_irq_edge =>
                    if g_edge_write then
                        iedge <= io_req_it.data;
                    end if;
                when c_itu_irq_clear =>
                    new_irq_edge_flag := new_irq_edge_flag and not io_req_it.data;
                when c_itu_timer =>
                    timer <= unsigned(io_req_it.data);
                when c_itu_irq_timer_en =>
                    irq_timer_en <= io_req_it.data(0);
                    irq_timer_select <= io_req_it.data(1);
                    if irq_timer_en='0' then
                        irq_timer_cnt <= irq_timer_val & X"FF";
                    end if;
                when c_itu_irq_timer_lo =>
                    irq_timer_val(7 downto 0) <= unsigned(io_req_it.data);
                when c_itu_irq_timer_hi =>
                    irq_timer_val(15 downto 8) <= unsigned(io_req_it.data);
                when others =>
                    null;
                end case;
            elsif io_req_it.read='1' then
                io_resp_it.ack  <= '1';

                case io_req_it.address(3 downto 0) is
                when c_itu_irq_global =>
                    io_resp_it.data(0) <= irq_en;
                when c_itu_irq_enable =>
                    io_resp_it.data <= imask;
                when c_itu_irq_edge =>
                    io_resp_it.data <= iedge;
                when c_itu_irq_active =>
                    io_resp_it.data <= irq_active;
                when c_itu_timer =>
                    io_resp_it.data <= std_logic_vector(timer);
                when c_itu_irq_timer_en =>
                    io_resp_it.data(0) <= irq_timer_en;
                    io_resp_it.data(1) <= irq_timer_select;
                when c_itu_irq_timer_lo =>
                    io_resp_it.data <= std_logic_vector(irq_timer_cnt(7 downto 0));
                when c_itu_irq_timer_hi =>
                    io_resp_it.data <= std_logic_vector(irq_timer_cnt(15 downto 8));
                when c_itu_fpga_version =>
                	io_resp_it.data <= std_logic_vector(g_version);
                when c_itu_capabilities0 =>
                    io_resp_it.data <= g_capabilities(31 downto 24);
                when c_itu_capabilities1 =>
                    io_resp_it.data <= g_capabilities(23 downto 16);
                when c_itu_capabilities2 =>
                    io_resp_it.data <= g_capabilities(15 downto 8);
                when c_itu_capabilities3 =>
                    io_resp_it.data <= g_capabilities( 7 downto 0);
                when others =>
                    null;
                end case;
            end if;

            for i in 0 to 7 loop
                if iedge(i)='1' then
                    if irq_c(i)='1' and irq_d(i)='0' then
                        new_irq_edge_flag(i) := '1';
                    end if;
                end if;
            end loop;
            irq_edge_flag <= new_irq_edge_flag;
            
            irq_out <= '0';
            if irq_en = '1' then
                if (irq_active and imask) /= X"00" then
                    irq_out <= '1';
                end if;
            end if;
                            
            if reset='1' then
                irq_en        <= '1';
                imask         <= (others => '0');
                iedge         <= g_edge_init;
                timer         <= (others => '0');
                irq_timer_en  <= '0';
                irq_timer_val <= X"8000";
                irq_timer_cnt <= (others => '0');
            end if;
        end if;
    end process;

    irq_active <= irq_edge_flag or (irq_c and not iedge);

    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 4,
        g_range_hi  => 4,
        g_ports     => 2 )
    port map (
        clock    => clock,
        
        req      => io_req,
        resp     => io_resp,
        
        reqs(0)  => io_req_it,
        reqs(1)  => io_req_uart,
        resps(0) => io_resp_it,
        resps(1) => io_resp_uart );

    r_uart: if g_uart generate
        uart: entity work.uart_peripheral_io
        generic map (
            g_divisor   => c_baud_div )
        port map (
            clock       => clock,
            reset       => reset,
            
            io_req      => io_req_uart,
            io_resp     => io_resp_uart,
            
            uart_irq    => uart_irq,
        
            rts         => uart_rts,
            cts         => uart_cts,
            txd         => uart_txd,
            rxd         => uart_rxd );
    end generate;

    no_uart: if not g_uart generate
        process(clock)
        begin
            if rising_edge(clock) then
                io_resp_uart <= c_io_resp_init;
                io_resp_uart.ack <= io_req_uart.read or io_req_uart.write;
            end if;
        end process;
    end generate;
    
end architecture;
