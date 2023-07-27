
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.core_pkg.all;

library work;
use work.tl_string_util_pkg.all;

library std;
use std.textio.all;

entity tb_cache is

end entity;

architecture run of tb_cache is
    signal cimem_o      : t_imem_req;
    signal cimem_i      : t_imem_resp;
    signal imem_o       : t_dmem_req;
    signal imem_i       : t_dmem_resp;
    signal fetch_i      : t_fetch_in;
    signal fetch_o      : t_fetch_out;
    signal reset        : std_logic;
    signal clock        : std_logic := '0';
    signal rdy_i        : std_logic := '0';
    signal imem_interval : natural := 5;
    signal expected     : std_logic_vector(31 downto 0) := (others => '0');
    signal wrong        : std_logic := '0';
    constant rdy_pattern : std_logic_vector := "01111110111110111101110110100000100001000100101111100000";
    type t_targets    is array(natural range <>) of std_logic_vector(31 downto 0);

    constant c_targets : t_targets := (
        X"00000000", X"00001000", X"00002000", X"00000000", X"00000FF0", X"00001FF0", X"00000030" );

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
        imem_o  => cimem_o,
        imem_i  => cimem_i,
        reset   => reset,
        rdy_i   => rdy_i,
        clock   => clock
    );

    i_cache: entity work.icache
      port map (
        clock   => clock,
        reset   => reset,
        disable => '0',
        dmem_i  => cimem_o,
        dmem_o  => cimem_i,
        mem_o   => imem_o,
        mem_i   => imem_i
      );


    process
    begin
        for i in rdy_pattern'range loop
            rdy_i <= '1'; --rdy_pattern(i);
            wait until clock='1';
        end loop;
    end process;

    -- generate jump
    process
    begin 
        for k in c_targets'range loop
            for m in 0 to 5 loop
                imem_interval <= m;
                for j in 1 to 10 loop
                    fetch_i.branch <= '0';
                    fetch_i.branch_target <= (others => 'X');
                    for i in 1 to 50+j loop 
                        wait until clock = '1';
                    end loop;
                    fetch_i.branch <= '1';
                    fetch_i.branch_target <= c_targets(k);
                    wait until clock = '1';
                end loop;
            end loop;
        end loop;
    end process;

    -- memory and IO
    process(clock)
        variable imem_l     : t_dmem_req;
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
                if fetch_o.program_counter /= expected then
                    wrong <= '1';
                end if;
                if fetch_o.instruction /= expected then
                    wrong <= '1';
                end if;
                expected <= std_logic_vector(unsigned(expected) + 4);
            end if;
            if fetch_i.branch = '1' then
                expected <= fetch_i.branch_target;
            end if;
        end if;
    end process;

end architecture;
