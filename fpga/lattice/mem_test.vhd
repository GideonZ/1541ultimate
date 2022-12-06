library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;

entity mem_test is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    ready       : in  std_logic;
        
    mem_req     : out t_mem_req_32;
    mem_resp    : in  t_mem_resp_32;
    
    ok_n        : out std_logic;
    error_n     : out std_logic );
end entity;

architecture rtl of mem_test is
    signal random   : std_logic_vector(31 downto 0);
    signal enable   : std_logic;
    signal preset   : std_logic;

    constant c_test_length : natural := 100;

    type t_state is (start, write, read, done);

    type s is record
        state    : t_state;
        count    : natural range 0 to c_test_length-1;
        address  : unsigned(mem_req.address'range);
        ok       : std_logic;
        error    : std_logic;
    end record;
    
    constant c_init : s := (
        state    => start,
        count    => c_test_length - 1,
        address  => (others => '0'),
        ok       => '0',
        error    => '0'
    );
    signal cur, nxt : s := c_init;
begin

    i_random: entity work.noise_generator
    generic map (
        g_polynom       => X"80200003",
        g_seed          => X"12345678" )

    port map (
        clock           => clock,
        enable          => enable,
        reset           => preset,
        q               => random
    );

    process(clock)
    begin
        if rising_edge(clock) then
            if reset = '1' then
                cur <= c_init;
            else
                cur <= nxt;
            end if;
        end if;
    end process;
    
    process(cur, mem_resp, random)
    begin        
        nxt <= cur;
        
        preset <= '0';
        enable <= '0';
        
        mem_req.tag <= X"01";
        mem_req.request <= '0';
        mem_req.address <= cur.address;
        mem_req.read_writen <= '1';
        mem_req.data <= random;
         
        case cur.state is
        when start =>
            nxt <= c_init;
            preset <= '1';
            nxt.state <= write;
        
        when write =>
            mem_req.request <= '1';
            mem_req.read_writen <= '0';

            if mem_resp.rack = '1' then
                nxt.address <= cur.address + 1;
                nxt.count <= cur.count - 1;
                enable <= '1';

                if cur.count = 0 then
                    nxt.state <= read;
                    nxt.address <= (others => '0');
                    preset <= '1';
                end if;
            end if;
                        
        when read =>
            mem_req.request <= '1';
            mem_req.read_writen <= '1';

            if mem_resp.rack = '1' then
                nxt.address <= cur.address + 1;
                nxt.count <= cur.count - 1;
                enable <= '1';
            
                if cur.count = 0 then
                    nxt.state <= done;
                end if;
            end if;

        when done =>
            nxt.ok <= not cur.error;

        end case;         

        if mem_resp.dack_tag(0) = '1' then
            if random /= mem_resp.data then
                nxt.error <= '1';
            end if;
        end if;

    end process;

    ok_n <= not cur.ok;
    error_n <= not cur.error;
    
end architecture;
