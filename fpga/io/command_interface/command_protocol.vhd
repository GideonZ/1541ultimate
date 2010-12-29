library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.io_bus_pkg.all;
    use work.slot_bus_pkg.all;
    use work.command_if_pkg.all;
    
entity command_protocol is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- io interface for local cpu
    io_req          : in  t_io_req; -- we get a 2K range
    io_resp         : out t_io_resp;

    -- C64 side interface
    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp;

    -- block memory
    address         : unsigned(10 downto 0);
    rdata           : std_logic_vector(7 downto 0);
    wdata           : std_logic_vector(7 downto 0);
    en              : std_logic;
    we              : std_logic );

end entity;


architecture gideon of command_protocol is
begin
    p_control: process(clock)
    begin
        if reset='1' then
        end if;
    end process;
end architecture;
