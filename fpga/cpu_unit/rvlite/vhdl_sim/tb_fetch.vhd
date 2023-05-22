
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
    signal imem_o       : imem_out_type;
    signal imem_i       : imem_in_type;
    signal fetch_i      : t_fetch_in;
    signal fetch_o      : t_fetch_out;
    signal rst_i        : std_logic;
    signal clk_i        : std_logic := '0';
    signal rdy_i        : std_logic := '0';
    signal rdy_timer    : natural := 5;
    signal imem_timer   : natural := 5;
    signal ack          : std_logic := '0';
    signal expected     : natural := 0;
    signal wrong        : std_logic := '0';
begin
    clk_i <= not clk_i after 10 ns;
    rst_i <= '1', '0' after 100 ns;

    i_fetch: entity work.fetch
    generic map (
        g_start_addr => X"00000000"
    )
    port map (
        fetch_o => fetch_o,
        fetch_i => fetch_i,
        imem_o  => imem_o,
        imem_i  => imem_i,
        rst_i   => rst_i,
        rdy_i   => rdy_i,
        clk_i   => clk_i
    );

    fetch_i.branch_target <= X"00000000";
    
    -- generate rdy
    process
    begin
        ack <= '0';
        if fetch_o.inst_valid = '0' then
            wait until fetch_o.inst_valid = '1';
        end if;
        for i in 0 to rdy_timer-1 loop
            wait until clk_i='1';
        end loop;
        ack <= '1';
        wait until clk_i='1';
    end process;
    rdy_i <= not fetch_o.inst_valid or ack;

    -- generate jump
    process
    begin 
        fetch_i.branch <= '0';
        for i in 1 to 50 loop 
            wait until clk_i = '1';
        end loop;
        fetch_i.branch <= '1';
        wait until clk_i = '1';
        if rdy_timer = 0 then
            rdy_timer <= 5;
            imem_timer <= imem_timer - 1;
        else
            rdy_timer <= rdy_timer - 1;
        end if;
    end process;


    -- memory and IO
    process(clk_i)
        variable imem_l     : imem_out_type;
        variable imem_count : natural;
    begin
        if rising_edge(clk_i) then
            if imem_i.ena_i = '1' then
                if imem_o.ena_o = '1' then
                    imem_l := imem_o;
                    imem_count := imem_timer;
                end if;
            end if;
            if imem_count /= 0 then
                imem_count := imem_count - 1;
                imem_i.ena_i <= '0';
            else
                imem_i.ena_i <= '1';
                imem_i.dat_i <= imem_l.adr_o;
            end if;

            if rst_i = '1' then
                imem_i.ena_i <= '1';
            end if;
        end if;
    end process;

    process(clk_i)
    begin
        if rising_edge(clk_i) then
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
