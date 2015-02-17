library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.nano_cpu_pkg.all;

entity nano_alu is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    value_in    : in  unsigned(15 downto 0);
    ext_in      : in  unsigned(15 downto 0);
    alu_oper    : in  std_logic_vector(15 downto 13);
    update_accu : in  std_logic;
    update_flag : in  std_logic;
    accu        : out unsigned(15 downto 0);
    z           : out boolean;
    n           : out boolean;
    c           : out boolean );
end entity;

architecture gideon of nano_alu is
    signal accu_i   : unsigned(15 downto 0) := (others => '0');
    signal alu_out  : unsigned(15 downto 0);
    signal alu_z    : boolean;
    signal alu_n    : boolean;
    signal carry    : std_logic;
    signal c_flag   : std_logic;
    signal add_oper2: unsigned(15 downto 0);
    signal add_res  : unsigned(17 downto 0);
    signal upd_carry: boolean;
begin
    with alu_oper select carry <=
        '0'     when c_alu_add,
        '1'     when c_alu_sub,
        c_flag  when c_alu_addc,
        '0'     when others;    

    with alu_oper select upd_carry <= 
        true    when c_alu_add,
        true    when c_alu_sub,
        true    when c_alu_addc,
        false   when others;    

    add_oper2 <= value_in when alu_oper(13)='0' else not value_in;        

    add_res <= ('0' & accu_i & '1') + ('0' & add_oper2 & carry);

    with alu_oper select alu_out <=
        value_in             when c_alu_load,
        value_in or accu_i   when c_alu_or,
        value_in and accu_i  when c_alu_and,
        value_in xor accu_i  when c_alu_xor,
        add_res(16 downto 1) when c_alu_add,
        add_res(16 downto 1) when c_alu_sub,
        add_res(16 downto 1) when c_alu_addc,
        ext_in               when others;

    alu_z <= (alu_out = 0);
    alu_n <= (alu_out(15)='1');

    process(clock)
    begin
        if rising_edge(clock) then
            if update_accu='1' then
                accu_i <= alu_out;
            end if;
            if update_flag='1' then
                z <= alu_z;
                n <= alu_n;
            end if;
            if upd_carry then
                c_flag <= add_res(17);
            end if;
            if reset='1' then
                c_flag <= '0';
            end if;
        end if;
    end process;
    
    accu <= accu_i;
    c <= (c_flag = '1');
end architecture;
