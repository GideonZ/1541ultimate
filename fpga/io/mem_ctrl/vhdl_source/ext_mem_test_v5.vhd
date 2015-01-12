-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple memory tester that can be
--              traced with chipscope.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.mem_bus_pkg.all;

entity ext_mem_test_v5 is
port (
    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';

    inhibit     : out std_logic := '0';

    req         : out t_mem_req_32;
    resp        : in  t_mem_resp_32;
    
    run         : out std_logic;
    okay        : out std_logic );
end entity;

architecture gideon of ext_mem_test_v5 is
    signal req_i    : t_mem_req_32;
    signal expected_data : std_logic_vector(31 downto 0);
    signal okay_i   : std_logic;
    signal run_i    : std_logic;    
    function xor_reduce(s : std_logic_vector) return std_logic is
        variable o : std_logic := '1';
    begin
        for i in s'range loop
            o := o xor s(i);
        end loop;
        return o;
    end function;
begin
    req <= req_i;
    okay <= okay_i;
    run <= run_i;
        
    p_test: process(clock)
    begin
        if rising_edge(clock) then
            if reset = '1' then 
                req_i <= c_mem_req_32_init;
                req_i.read_writen <= '0'; -- write
                req_i.request <= '1';
                req_i.size <= '1';
                req_i.data <= X"12345678";
                req_i.byte_en <= "1000";
                req_i.tag <= X"34";
                okay_i <= '1';
                run_i <= '0';
            else        
                if resp.rack='1' then
                    if req_i.read_writen = '1' then
                        if signed(req_i.address) = -4 then
                            req_i.data <= not req_i.data;
                            run_i <= not run_i;
                        end if;
                        req_i.address <= req_i.address + 4;
                    else
                        expected_data <= req_i.data;
                    end if;
                    req_i.read_writen <= not req_i.read_writen;
                end if;

                if resp.dack_tag = X"34" then
                    if resp.data /= expected_data then
                        okay_i <= not okay_i;
                    end if;
                end if;
            end if;
        end if;
    end process;


end architecture;
