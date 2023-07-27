
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.core_pkg.all;

library work;
use work.tl_string_util_pkg.all;

library std;
use std.textio.all;

entity tb_fetch is

end entity;

architecture run of tb_fetch is
    signal imem_o       : t_imem_req;
    signal imem_i       : t_imem_resp;
    signal fetch_i      : t_fetch_in;
    signal fetch_o      : t_fetch_out;
    signal reset        : std_logic;
    signal clock        : std_logic := '0';
    signal rdy_i        : std_logic := '0';
    signal rdy_interval  : natural := 5;
    signal imem_interval : natural := 5;
    signal ack          : std_logic := '0';
    signal expected     : natural := 0;
    signal wrong        : std_logic := '0';
    constant rdy_pattern : std_logic_vector := "01111110111110111101110110100000100001000100101111100000";

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_fetch: entity work.fetch
    generic map (
        g_start_addr => X"00000000"
    )
    port map (
        fetch_o => fetch_o,
        fetch_i => fetch_i,
        imem_o  => imem_o,
        imem_i  => imem_i,
        reset   => reset,
        rdy_i   => rdy_i,
        clock   => clock
    );

    fetch_i.branch_target <= X"00000000";
    
    -- -- generate rdy
    -- process
    -- begin
    --     ack <= '0';
    --     if fetch_o.inst_valid = '0' then
    --         wait until fetch_o.inst_valid = '1';
    --     end if;
    --     for i in 0 to rdy_interval-1 loop
    --         wait until clock='1';
    --     end loop;
    --     ack <= '1';
    --     wait until clock='1';
    -- end process;
    -- rdy_i <= '1';--not fetch_o.inst_valid or ack;
    process
    begin
        for i in rdy_pattern'range loop
            rdy_i <= rdy_pattern(i);
            wait until clock='1';
        end loop;
    end process;

    -- generate jump
    process
    begin 
        for j in 1 to 10 loop
            fetch_i.branch <= '0';
            for i in 1 to 50+j loop 
                wait until clock = '1';
            end loop;
            fetch_i.branch <= '1';
            wait until clock = '1';
        end loop;
        if rdy_interval = 0 then
            rdy_interval <= 5;
            imem_interval <= imem_interval - 1;
        else
            rdy_interval <= rdy_interval - 1;
        end if;
    end process;


    -- memory and IO
    process(clock)
        variable imem_l     : t_imem_req;
        variable imem_count : natural;
    begin
        if rising_edge(clock) then
            if imem_i.ena_i = '1' then
                if imem_o.ena_o = '1' then
                    imem_l := imem_o;
                    imem_count := imem_interval;
                end if;
            end if;
            if imem_count /= 0 then
                imem_count := imem_count - 1;
                imem_i.ena_i <= '0';
            else
                imem_i.ena_i <= '1';
                imem_i.dat_i <= imem_l.adr_o;
            end if;

            if reset = '1' then
                imem_i.ena_i <= '1';
            end if;
        end if;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            if rdy_i = '1' and fetch_o.inst_valid = '1' then
                if fetch_o.program_counter /= std_logic_vector(to_unsigned(expected, 32)) then
                    wrong <= '1';
                end if;
                if fetch_o.instruction /= std_logic_vector(to_unsigned(expected, 32)) then
                    wrong <= '1';
                end if;
                expected <= expected + 4;
            end if;
            if fetch_i.branch = '1' then
                expected <= 0;
            end if;
        end if;
    end process;

end architecture;
