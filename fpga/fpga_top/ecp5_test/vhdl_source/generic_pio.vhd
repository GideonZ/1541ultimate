library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity generic_pio is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

    pio_i       : in  std_logic_vector(63 downto 0);
    pio_o       : out std_logic_vector(63 downto 0);
    pio_t       : out std_logic_vector(63 downto 0) 
);
end entity;

architecture gideon of generic_pio is
    signal pio_t_i  : std_logic_vector(63 downto 0);
begin
    process(clock)
        variable v_addr : natural range 0 to 15;
    begin
        if rising_edge(clock) then
            v_addr := to_integer(io_req.address(2 downto 0));
            io_resp <= c_io_resp_init;

            if io_req.write = '1' then
                io_resp.ack <= '1';
                if io_req.address(3)='0' then
                    pio_o(7 + 8*v_addr downto 8*v_addr) <= io_req.data;
                else
                    pio_t_i(7 + 8*v_addr downto 8*v_addr) <= io_req.data;
                end if;
            elsif io_req.read = '1' then
                io_resp.ack <= '1';
                if io_req.address(3)='0' then
                    io_resp.data <= pio_i(7 + 8*v_addr downto 8*v_addr);
                else
                    io_resp.data <= pio_t_i(7 + 8*v_addr downto 8*v_addr);
                end if;
            end if;

            if reset = '1' then
                pio_t_i <= (others => '0');
                pio_o <= (others => '1');
            end if;
        end if;
    end process;

    pio_t <= pio_t_i;

end architecture;
