library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.pkg_6502_defs.all;
use work.tl_flat_memory_model_pkg.all;
use work.pkg_6502_decode.all;

entity tb_proc_core is
generic (
    test_file : string := "opcode_test";
    test_base : integer := 16#02FE# );
end tb_proc_core;

architecture tb of tb_proc_core is

    signal clock        : std_logic := '0';
    signal clock_en     : std_logic;
    signal reset        : std_logic;
    signal addr_out     : std_logic_vector(19 downto 0) := X"00000";
    signal data_in      : std_logic_vector(7 downto 0) := X"AA";
    signal data_out     : std_logic_vector(7 downto 0);
    signal i_reg        : std_logic_vector(7 downto 0);
    signal read_write_n : std_logic;
    signal nmi_n        : std_logic := '1';
    signal irq_n        : std_logic := '1';
    signal stop_clock   : boolean := false;
    shared variable ram : h_mem_object;
    signal brk          : std_logic := '0';
    signal cmd          : std_logic := '0';

    signal s_is_absolute         :  boolean;
    signal s_is_abs_jump         :  boolean;
    signal s_is_immediate        :  boolean;    
    signal s_is_implied          :  boolean;
    signal s_is_stack            :  boolean;
    signal s_is_push             :  boolean;
    signal s_is_zeropage         :  boolean;
    signal s_is_indirect         :  boolean;
    signal s_is_relative         :  boolean;
    signal s_is_load             :  boolean;
    signal s_is_store            :  boolean;
    signal s_is_shift            :  boolean;
    signal s_is_alu              :  boolean;
    signal s_is_rmw              :  boolean;
    signal s_is_jump             :  boolean;
    signal s_is_postindexed      :  boolean;
    signal s_is_illegal          :  boolean;
    signal s_select_index_y      :  boolean;
    signal s_store_a_from_alu    :  boolean;
    signal s_load_a              :  boolean;    
    signal s_load_x              :  boolean;    
    signal s_load_y              :  boolean;    

begin

    clock    <= not clock after 50 ns when not stop_clock;
    clock_en <= '1';
    reset    <= '1', '0' after 300 ns;

    core: entity work.proc_core
    generic map (
        support_bcd  => true )
    port map (
        clock        => clock,
        clock_en     => clock_en,
        reset        => reset,

        irq_n        => irq_n,
        nmi_n        => nmi_n,
    
        addr_out     => addr_out(16 downto 0),
        inst_out     => i_reg,
        data_in      => data_in,
        data_out     => data_out,
        read_write_n => read_write_n );

    cmd <= '1' when addr_out = X"10391" else '0';
    
    process(clock)
        variable addr : std_logic_vector(31 downto 0) := (others => '0');
    begin
        if falling_edge(clock) then
            addr(15 downto 0) := addr_out(15 downto 0);
            data_in <= read_memory_8(ram, addr);

            if read_write_n = '0' then
                write_memory_8(ram, addr, data_out);
--                if addr_out(15 downto 0) = X"FFF8" then
--                    stop_clock <= true;
--                    if data_out = X"55" then
--                        report "Test program completed successfully." severity note;
--                    else
--                        report "Test program failed." severity error;
--                    end if;
--                elsif addr_out(15 downto 0) = X"FFF9" then
--                    case data_out is
--                    when X"01" =>  report "Break IRQ service routine." severity note;
--                    when X"02" =>  report "External IRQ service routine." severity note;
--                    when X"03" =>  report "NMI service routine." severity note;
--                    when others => report "Unknown event message." severity warning;
--                    end case;
--                end if;                       
            else -- read
                if addr_out = X"1FFFE" then -- vector for BRK
                    brk <= '1';
                end if;
            end if;
        end if;
    end process;

    test: process
    begin
        register_mem_model(tb_proc_core'path_name, "6502 ram", ram);        
        load_memory("bootstrap", ram, X"0000FFEE");
        load_memory(test_file, ram, std_logic_vector(to_unsigned(test_base, 32)));
        wait until brk = '1';
        save_memory("result", ram, X"00000000", 2048);
        stop_clock <= true;
--        wait for 30 us;
--        irq_n <= '0';
--        wait for 10 us;
--        irq_n <= '1';
--        wait for 10 us;
--        nmi_n <= '0';
--        wait for 10 us;
--        nmi_n <= '1';
        wait;
    end process;


    s_is_absolute         <= is_absolute(i_reg);
    s_is_abs_jump         <= is_abs_jump(i_reg);
    s_is_immediate        <= is_immediate(i_reg);    
    s_is_implied          <= is_implied(i_reg);
    s_is_stack            <= is_stack(i_reg);
    s_is_push             <= is_push(i_reg);
    s_is_zeropage         <= is_zeropage(i_reg);
    s_is_indirect         <= is_indirect(i_reg);
    s_is_relative         <= is_relative(i_reg);
    s_is_load             <= is_load(i_reg);
    s_is_store            <= is_store(i_reg);
    s_is_shift            <= is_shift(i_reg);
    s_is_alu              <= is_alu(i_reg);
    s_is_rmw              <= is_rmw(i_reg);
    s_is_jump             <= is_jump(i_reg);
    s_is_postindexed      <= is_postindexed(i_reg);
    s_is_illegal          <= is_illegal(i_reg);
    s_select_index_y      <= select_index_y(i_reg);
    s_store_a_from_alu    <= store_a_from_alu(i_reg);
    s_load_a              <= load_a(i_reg);
    s_load_x              <= load_x(i_reg);
    s_load_y              <= load_y(i_reg);


end tb;
