-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple memory tester that can be
--              traced with chipscope. This is the 32 bit version.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.mem_bus_pkg.all;

entity ext_mem_test_32 is
port (
    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';

    inhibit     : out std_logic := '0';

    req         : out t_mem_burst_32_req;
    resp        : in  t_mem_burst_32_resp;
    
    okay        : out std_logic );
end entity;

architecture gideon of ext_mem_test_32 is
    type t_access is record
        address     : unsigned(27 downto 0);
        read_writen : std_logic;
    end record;
    type t_accesses is array (natural range <>) of t_access;
    constant c_test_vector : t_accesses := (
        ( X"0000000", '0' ), -- write to 0
        ( X"0000100", '0' ), -- write to 100
        ( X"0000000", '1' ), -- read from 0
        ( X"0010000", '0' ), -- write to 64K        
        ( X"0000100", '1' ), -- read from 100
        ( X"0010000", '1' ) ); -- read from 64K        

    subtype t_data is std_logic_vector(31 downto 0);
    type t_datas is array (natural range <>) of t_data;
    constant c_test_data : t_datas(0 to 7) := (
        X"12345678", X"9ABCDEF0",   -- 0
        X"DEADBEEF", X"C0EDBABE",   -- 100
        X"00FF00FF", X"00FF00FF",   -- 64K
        X"55AA55AA", X"3366CC99" ); -- 0, etc

    signal data_count   : integer range 0 to c_test_data'high;
    signal check_count  : integer range 0 to c_test_data'high;
    signal cmd_count    : integer range 0 to c_test_vector'high;

begin
    process(clock)
    begin
        if rising_edge(clock) then
            if reset='1' then
                req <= c_mem_burst_32_req_init;
                data_count <= 0;
                cmd_count <= 0;
                check_count <= 0;
                okay <= '1';
            else
                -- push write data
                if resp.wdata_full='0' then
                    req.data      <= c_test_data(data_count);
                    req.byte_en   <= (others => '1');
                    req.data_push <= '1';

                    if data_count = c_test_data'high then
                        data_count <= 0;
                    else
                        data_count <= data_count + 1;
                    end if;
                else
                    req.data_push <= '0';
                end if;

                -- push commands
                req.request     <= '1';
                if resp.ready='1' then
                    req.request_tag <= std_logic_vector(to_unsigned(cmd_count, 8));
                    req.address     <= c_test_vector(cmd_count).address(25 downto 0);
                    req.read_writen <= c_test_vector(cmd_count).read_writen;
                    if cmd_count = c_test_vector'high then
                        cmd_count <= 0;
                    else
                        cmd_count <= cmd_count + 1;
                    end if;
                end if;

                -- check read data
                if resp.rdata_av='1' then
                    if resp.data = c_test_data(check_count) then
                        okay <= '1';
                    else
                        okay <= '0';
                    end if;
                    if check_count = c_test_data'high then
                        check_count <= 0;
                    else
                        check_count <= check_count + 1;
                    end if;
                end if;
            end if;
        end if;
    end process;
    
end architecture;

