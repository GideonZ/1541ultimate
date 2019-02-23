library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library mblite;
use mblite.config_Pkg.all;
use mblite.core_Pkg.all;
use mblite.std_Pkg.all;

library work;
use work.tl_string_util_pkg.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

library std;
use std.textio.all;

entity mblite_simu_wrapped is

end entity;

architecture test of mblite_simu_wrapped is

    signal clock  : std_logic := '0';
    signal reset  : std_logic;
    signal mb_reset    : std_logic := '1';
    signal mem_req     : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp    : t_mem_resp_32;
    signal io_req      : t_io_req;
    signal io_resp     : t_io_resp;
    signal io_busy     : std_logic;
    
    signal irq_i  : std_logic := '0';
    signal irq_o  : std_logic;
    signal irqs   : std_logic_vector(7 downto 0) := X"00";
    signal plaag_interrupt  : std_logic := '0';    
    signal invalidate   : std_logic := '0';
    signal inv_addr     : std_logic_vector(31 downto 0) := X"00003FFC";
    
    type t_mem_array is array(natural range <>) of std_logic_vector(31 downto 0);
    
    shared variable memory  : t_mem_array(0 to 4194303) := (1024 => X"00000000", others => X"F0000000"); -- 16MB
    signal last_char : std_logic_vector(7 downto 0);
    constant g_latency : natural := 1;
    signal pipe         : t_mem_req_32_array(0 to g_latency-1) := (others => c_mem_req_32_init);
    signal isr, yield, leave1, leave2   : std_logic := '0';
    signal task : std_logic_vector(7 downto 0) := X"FF";
    signal curTCB, save, restore, critical : std_logic_vector(31 downto 0) := (others => 'X');
BEGIN
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    mb_reset <= '1', '0' after 2 us;
    
    --plaag_interrupt <= transport '0', '1' after 25 ms, '0' after 30 ms; 
    i_core: entity work.mblite_wrapper
    generic map (
        g_tag_i    => X"0A",
        g_tag_d    => X"0B"
    )
    port map (
        clock      => clock,
        reset      => reset,
        irq_i      => irq_i,
        irq_o      => irq_o,
        mb_reset   => mb_reset,
        disable_i  => '0',
        disable_d  => '0',
        invalidate => invalidate,
        inv_addr   => inv_addr,
        io_req     => io_req,
        io_resp    => io_resp,
        io_busy    => io_busy,
        mem_req    => mem_req,
        mem_resp   => mem_resp
    );
    
    process
    begin
        wait until reset='0';
        wait until clock='1';
        wait until clock='1';
        while true loop
            invalidate <= '0';
            wait until clock='1';
            invalidate <= '0';
            wait until clock='1';
            wait until clock='1';
        end loop;        
    end process;

    -- IO
    process(clock)
        variable s    : line;
        variable char : character;
        variable byte : std_logic_vector(7 downto 0);
        variable irqdiv  : natural := 50000;
        variable irqdiv2 : natural := 60000;
        variable not_ready : natural := 3;
    begin
        if rising_edge(clock) then
            if irqdiv = 0 then
                irqs(0) <= '1';
                irqdiv := 400000;
            else
                irqdiv := irqdiv - 1;
            end if;

--            if irqdiv2 = 0 then
--                irqs(2) <= '1';
--                irqdiv2 := 97127;
--            else
--                irqdiv2 := irqdiv2 - 1;
--            end if;

            io_resp <= c_io_resp_init;
            leave1 <= '0';
            leave2 <= '0';
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case io_req.address is
                when X"00028" => -- enter IRQ
                    isr <= '1';
                when X"00029" => -- enter yield
                    yield <= '1';
                when X"0002A" => -- leave 
                    isr <= '0';
                    yield <= '0';
                    if io_req.data(1) = '1' then
                        leave1 <= '1'; -- with IRQ ON
                    else
                        leave2 <= '1'; -- with IRQ OFF
                    end if;
                when X"00000" => -- interrupt
                    null;
                when X"60304" => -- profiler
                    null;
                when X"60305" => -- profiler
                    task <= io_req.data;
                    null;
                when X"00004" => -- interupt ack
                    irqs <= irqs and not io_req.data;
                when X"00010" => -- UART_DATA
                    not_ready := 500;
                    byte := io_req.data;
                    char := character'val(to_integer(unsigned(byte)));
                    last_char <= byte;
                    if byte = X"0D" then
                        -- Ignore character 13
                    elsif byte = X"0A" then
                        -- Writeline on character 10 (newline)
                        writeline(output, s);
                    else
                        -- Write to buffer
                        write(s, char);
                    end if;
                when others =>
                    null;
--                    report "I/O write to " & hstr(io_req.address) & " dropped";
                end case;
            end if;

            if io_req.read = '1' then
                io_resp.ack <= '1';             
                case io_req.address is
                when X"00005" => -- IRQ Flags
                    io_resp.data <= irqs;
                when X"0000C" => -- Capabilities 11D447B9
                    io_resp.data <= X"11";
                when X"0000D" => -- Capabilities 11D447B9
                    io_resp.data <= X"D4";
                when X"0000E" => -- Capabilities 11D447B9
                    io_resp.data <= X"47";
                when X"0000F" => -- Capabilities 11D447B9
                    io_resp.data <= X"99";
                when X"00012" => -- UART_FLAGS
                    if not_ready > 0 then
                        io_resp.data <= X"50";
                        not_ready := not_ready - 1;
                    else
                        io_resp.data <= X"40";
                    end if;
                when X"2000A" => -- 1541_A memmap
                    io_resp.data <= X"3F";
                when X"2000B" => -- 1541_A audiomap
                    io_resp.data <= X"3E";
                when others =>
--                    report "I/O read to " & hstr(io_req.address) & " dropped";
                    io_resp.data <= X"00";
                end case;
            end if;

            irqs(7) <= plaag_interrupt;

            if reset = '1' then
                irqs <= X"00";
            end if;
        end if;
    end process;
   
    -- memory
    process(clock)
        variable addr : natural;
    begin
        if rising_edge(clock) then
            mem_resp <= c_mem_resp_32_init;
            pipe(g_latency-1) <= c_mem_req_32_init;
            if mem_req.request = '1' and mem_resp.rack = '0' then
                mem_resp.rack <= '1';
                mem_resp.rack_tag <= mem_req.tag;
                pipe(g_latency-1) <= mem_req;
                assert mem_req.address(1 downto 0) = "00" report "Lower address bits are non-zero!" severity warning;

                if mem_req.read_writen = '0' then
                    case to_integer(mem_req.address) is
                    when 16|20 =>
                        report "Write to illegal address!"
                        severity warning;
                        
--                    when 16#16d20# =>
--                        report "Debug: Writing Current TCB to " & hstr(mem_req.data);
--                        curTCB <= mem_req.data;
----                    when 16#16954# =>
----                        critical <= mem_req.data;
--                    when 16#FFC# =>
--                        report "Debug: Save context, TCB = " & hstr(mem_req.data);
--                        save <= mem_req.data;
--                    when 16#FF8# =>
--                        report "Debug: Restore context, TCB = " & hstr(mem_req.data);
--                        restore <= mem_req.data;
                    when others =>
                    end case;
                end if;
            end if;

            pipe(0 to g_latency-2) <= pipe(1 to g_latency-1);
            mem_resp.dack_tag <= (others => '0');
            mem_resp.data     <= (others => '0');

            if pipe(0).request='1' then
                addr := to_integer(pipe(0).address(21 downto 2));
                if pipe(0).read_writen='1' then
                    mem_resp.dack_tag <= pipe(0).tag;
                    mem_resp.data     <= memory(addr);
                else
                    for i in 0 to 3 loop
                        if pipe(0).byte_en(i)='1' then
                            memory(addr)(i*8+7 downto i*8) := pipe(0).data(i*8+7 downto i*8);
                        end if;
                    end loop;
                end if;
            end if;
        end if; 
    end process;
   
   irq_i <= '1' when irqs /= X"00" else '0';
end architecture;
