library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity delayed_write is
generic (
    g_address_width     : natural := 26;
    g_data_width        : natural := 8 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    match_req   : in  std_logic;
    match_addr  : in  unsigned(g_address_width-1 downto 0);

    bus_address : in  unsigned(g_address_width-1 downto 0);
    bus_write   : in  std_logic;
    bus_wdata   : in  std_logic_vector(g_data_width-1 downto 0);
    invalidate  : in  std_logic;
    
    reg_address : out unsigned(g_address_width-1 downto 0);
    reg_out     : out std_logic_vector(g_data_width-1 downto 0);
    reg_valid   : out std_logic;
    reg_hit     : out std_logic );
end delayed_write;

architecture gideon of delayed_write is
    signal valid        : std_logic;
    signal reg_data     : std_logic_vector(bus_wdata'range) := (others => '0');
    signal reg_addr_i   : unsigned(bus_address'range) := (others => '0');
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if bus_write='1' then
                reg_data    <= bus_wdata;
                reg_addr_i  <= bus_address;
                valid       <= '1';
            elsif invalidate='1' then
                valid       <= '0';
            end if;
            
            reg_hit <= '0';
            if valid='1' and reg_addr_i = match_addr and match_req='1' then
                reg_hit <= '1';
            end if;
            
            if reset='1' then
                valid   <= '0';
                reg_hit <= '0';
            end if;
        end if;
    end process;

    reg_out     <= reg_data;
    reg_address <= reg_addr_i;
    reg_valid   <= valid;   
end architecture;
