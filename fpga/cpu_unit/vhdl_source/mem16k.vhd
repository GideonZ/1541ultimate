library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mem16k is
generic (
    simulation  : boolean := false );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    address     : in  std_logic_vector(26 downto 0);
    request     : in  std_logic;
    mwrite      : in  std_logic;
    wdata       : in  std_logic_vector(7 downto 0);
    rdata       : out std_logic_vector(7 downto 0);
    rack        : out std_logic;
    dack        : out std_logic;
    claimed     : out std_logic );

    attribute keep_hierarchy : string;
    attribute keep_hierarchy of mem16k : entity is "yes";
end mem16k;


architecture gideon of mem16k is
    subtype t_byte      is std_logic_vector(7 downto 0);
    type t_byte_array   is array(natural range <>) of t_byte;
    shared variable my_mem : t_byte_array(0 to 16383);
    signal claimed_i    : std_logic;
    signal do_write     : std_logic;
    
--    attribute ram_style : string;
--    attribute ram_style of my_mem : signal is "block";
begin
    claimed_i <= '1' when address(26 downto 14) = "0000000000000"
            else '0';
    claimed   <= claimed_i;          
    rack      <= claimed_i and request;
    do_write  <= claimed_i and request and mwrite;

    -- synthesis translate_off
    model: if simulation generate
        mram: entity work.bram_model_8sp
        generic map("intram", 14) -- 16k
        port map (
    		CLK  => clock,
    		SSR  => reset,
    		EN   => request,
    		WE   => do_write,
            ADDR => address(13 downto 0),
    		DI   => wdata,
    		DO   => rdata );
    end generate;        
    -- synthesis translate_on

    process(clock)
    begin
        if rising_edge(clock) then
            if do_write='1' then
                my_mem(to_integer(unsigned(address(13 downto 0)))) := wdata;
            end if;
            if not simulation then
                rdata <= my_mem(to_integer(unsigned(address(13 downto 0))));
            else
                rdata <= (others => 'Z');
            end if;
            dack  <= claimed_i and request;
        end if;
    end process;
    
end gideon;
