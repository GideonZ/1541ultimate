library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.tl_string_util_pkg.all;

library std;
use std.textio.all;

entity tb_alu is

end entity;

architecture test of tb_alu is
    type t_words is array(natural range <>) of std_logic_vector(31 downto 0);
    constant c_test_vector : t_words := (
        X"FFFFFFFC", X"FFFFFFFD", X"FFFFFFFE", X"FFFFFFFF", 
        X"00000000", X"00000001", X"00000002", X"00000003",
        X"7FFFFFFE", X"7FFFFFFF", X"80000000", X"80000001" ); 

    signal data_1       : std_logic_vector(31 downto 0); -- register rs1
    signal data_2       : std_logic_vector(31 downto 0); -- register rs2 or imm
    signal oper         : std_logic_vector(2 downto 0);
    signal func         : std_logic;
    signal branch_taken : std_logic;
    signal data_out     : std_logic_vector(31 downto 0);

begin
    i_alu: entity work.alu_branch
    port map (
        data_1       => data_1,
        data_2       => data_2,
        oper         => oper,
        func         => func,
        branch_taken => branch_taken,
        data_out     => data_out
    );

    process
        variable s : line;
    begin
        oper <= "000";
        func <= '1';
        for a in c_test_vector'range loop
            for b in c_test_vector'range loop
                write(s, hstr(c_test_vector(a)) & " ");
                write(s, hstr(c_test_vector(b)) & " ");
                for f in 0 to 7 loop
                    data_1 <= c_test_vector(a);
                    data_2 <= c_test_vector(b);
                    oper <= std_logic_vector(to_unsigned(f, 3));
                    wait for 10 ns;
                    write(s, hstr(data_out) & " ");
                    write(s, std_logic'image(branch_taken) & " ");
                end loop;
                writeline(output, s);
            end loop;
        end loop;
        wait;
    end process;

end architecture;

