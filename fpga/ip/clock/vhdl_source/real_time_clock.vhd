library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity real_time_clock is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;    
    tick_1kHz   : in  std_logic;
    req         : in  t_io_req;
    resp        : out t_io_resp );
end entity;

architecture gideon of real_time_clock is
    signal ms_count     : unsigned(9 downto 0) := (others => '0');
    signal sec_tick     : std_logic;
    signal sec_count    : unsigned(31 downto 0) := X"67829644";
    signal lock         : std_logic;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            sec_tick <= '0';
            if tick_1kHz = '1' and lock = '0' then
                if ms_count = 999 then
                    sec_tick <= '1';
                    ms_count <= (others => '0');
                else
                    ms_count <= ms_count + 1;
                end if;
            end if;
            if sec_tick = '1' then
                sec_count <= sec_count + 1;
            end if;

            resp <= c_io_resp_init;
            if req.read='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    resp.data <= std_logic_vector(sec_count(7 downto 0));
                when X"1" =>
                    resp.data <= std_logic_vector(sec_count(15 downto 8));
                when X"2" =>
                    resp.data <= std_logic_vector(sec_count(23 downto 16));
                when X"3" =>
                    resp.data <= std_logic_vector(sec_count(31 downto 24));
                when others =>
                    null;
                end case;
            elsif req.write='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    sec_count(7 downto 0) <= unsigned(req.data);
                    lock <= '1';
                when X"1" =>
                    sec_count(15 downto 8) <= unsigned(req.data);
                when X"2" =>
                    sec_count(23 downto 16) <= unsigned(req.data);
                when X"3" =>
                    sec_count(31 downto 24) <= unsigned(req.data);
                    lock <= '0';
                when others =>
                    null;
                end case;
            end if;
            if reset = '1' then
                lock <= '0';
            end if;
        end if;
    end process;
end architecture;
    
