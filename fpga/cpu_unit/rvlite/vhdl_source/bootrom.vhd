
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.mem_bus_pkg.all;
use work.bootrom_pkg.all;

entity bootrom is
port
(
    clock       : in  std_logic;
    reset       : in  std_logic;
    ireq        : in  t_mem_req_32;
    iresp       : out t_mem_resp_32;
    oreq        : out t_mem_req_32;
    oresp       : in  t_mem_resp_32
);
end entity;

architecture arch of bootrom is
    signal boot_cs      : std_logic;
    signal boot_valid   : std_logic;
    signal boot_write   : std_logic;
    signal boot_tag     : std_logic_vector(7 downto 0) := X"20";
    signal boot_data    : std_logic_vector(31 downto 0);
    signal boot_ram     : t_boot_data(0 to 1023) := c_bootrom;
begin
    process(ireq, oresp, boot_cs, boot_valid)
    begin
        oreq <= ireq;
        iresp <= oresp;
        if boot_cs = '1' then
            oreq.request <= '0';
            iresp.rack <= '1';
            iresp.rack_tag <= ireq.tag;
        end if;
        if boot_valid = '1' and oresp.dack_tag = X"00" then
            iresp.dack_tag <= boot_tag;
            iresp.data <= boot_data;
        end if;
    end process;
    boot_cs <= '1' when ireq.address(ireq.address'high downto 12) = 0 and ireq.request = '1' else '0';

    process(clock)
    begin
        if rising_edge(clock) then
            if oresp.dack_tag = X"00" then
                boot_valid <= '0';
            end if;
            if boot_cs = '1' then
                boot_valid <= '1';
                boot_tag <= ireq.tag;
            end if;
            if boot_cs = '1' and ireq.read_writen = '0' and ireq.byte_en /= X"F" then
                report "Unsupported write to bootram"
                severity error;
            end if;
            if boot_cs = '1' and ireq.read_writen = '0' and ireq.address(11 downto 8) < 14 then
                report "Write to bootrom code!!!"
                severity failure;
            end if;
            if reset = '1' then
                boot_tag <= X"20";
            end if;
        end if;
    end process;

    boot_write <= '1' when ireq.read_writen = '0' and ireq.address(11 downto 9) = "111" else '0';
    process(clock)
    begin
        if rising_edge(clock) then
            if boot_cs = '1' then
                if boot_write = '1' then
                    boot_ram(to_integer(ireq.address(11 downto 2))) <= ireq.data;
                end if;
                boot_data <= boot_ram(to_integer(ireq.address(11 downto 2)));
            end if;
        end if;
    end process;    
end architecture;
