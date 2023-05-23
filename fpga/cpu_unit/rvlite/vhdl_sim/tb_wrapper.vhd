
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

library work;
use work.tl_string_util_pkg.all;
use work.tl_flat_memory_model_pkg.all;

library std;
use std.textio.all;

entity tb_wrapper is

end entity;

architecture run of tb_wrapper is
    signal clock        : std_logic := '1';
    signal reset        : std_logic;
    signal mem_req      : t_mem_req_32;
    signal mem_resp     : t_mem_resp_32;
    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    signal irq_i        : std_logic := '0';
	signal last_char   : character;

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_cpu: entity work.rvlite_wrapper
    generic map (
        g_icache => true,
        g_tag_i  => X"20",
        g_tag_d  => X"21"
    )
    port map (
        clock    => clock,
        reset    => reset,
        irq_i    => irq_i,
        io_req   => io_req,
        io_resp  => io_resp,
        io_busy  => open,
        mem_req  => mem_req,
        mem_resp => mem_resp
    );

    i_mem: entity work.mem_bus_32_slave_bfm
    generic map (
        g_name        => "dram",
        g_time_to_ack => 1,
        g_latency     => 3
    )
    port map (
        clock => clock,
        req   => mem_req,
        resp  => mem_resp
    );

    process
        variable mem: h_mem_object;
    begin
        wait for 1 ns;
        bind_mem_model("dram", mem);
        load_memory("../../../../target/software/riscv32_hello/result/hello_world.bin", mem, X"00000000");
        wait;
    end process;

    -- IO
    process(clock)
        variable s      : line;
        variable char   : character;
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;
            if io_req.read = '1' then
                io_resp.ack <= '1';
                case io_req.address is
                when X"00000C" => -- Capabilities
                    io_resp.data <= X"02";
                when X"000012" => -- UART_FLAGS
                    io_resp.data <= X"40";
                when X"02000A" => -- 1541_A memmap
                    io_resp.data <= X"3F";
                when X"02000B" => -- 1541_A audiomap
                    io_resp.data <= X"3E";
                when others =>
                    report "I/O read to " & hstr(io_req.address) & " dropped";
                    io_resp.data <= X"00";
                end case;
            end if;
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case io_req.address is
                when X"000010" => -- UART_DATA
                    char := character'val(to_integer(unsigned(io_req.data)));
                    last_char <= char;
                    if io_req.data = X"0D" then
                        -- Ignore character 13
                    elsif io_req.data = X"0A" then
                        -- Writeline on character 10 (newline)
                        writeline(output, s);
                        irq_i <= '1' after 5 us;
                    else
                        -- Write to buffer
                        write(s, char);
                    end if;
                when others =>
                    report "I/O write to " & hstr(io_req.address) & " dropped";
                end case;
            end if;
        end if;
    end process;
    
end architecture;
