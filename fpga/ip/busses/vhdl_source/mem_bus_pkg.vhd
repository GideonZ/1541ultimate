library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package mem_bus_pkg is

    type t_mem_req is record
        tag         : std_logic_vector(7 downto 0);
        request     : std_logic;
        read_writen : std_logic;
        size        : unsigned(1 downto 0); -- +1 for reads only
        address     : unsigned(25 downto 0);
        data        : std_logic_vector(7 downto 0);
    end record;
    
    type t_mem_resp is record
        data        : std_logic_vector(7 downto 0);
        rack        : std_logic;
        rack_tag    : std_logic_vector(7 downto 0);
        dack_tag    : std_logic_vector(7 downto 0);
        count       : unsigned(1 downto 0);
    end record;

    constant c_mem_req_init : t_mem_req := (
        tag         => X"00",
        request     => '0',
        read_writen => '1',
        size        => "00",
        address     => (others => '0'),
        data        => X"00" );
     
    constant c_mem_resp_init : t_mem_resp := (
        data        => X"00",
        rack        => '0',
        rack_tag    => X"00",
        dack_tag    => X"00",
        count       => "00" );
        
    type t_mem_req_array is array(natural range <>) of t_mem_req;
    type t_mem_resp_array is array(natural range <>) of t_mem_resp;

    ----
    type t_mem_req_32 is record
        tag         : std_logic_vector(7 downto 0);
        request     : std_logic;
        read_writen : std_logic;
        address     : unsigned(25 downto 0);
        byte_en     : std_logic_vector(3 downto 0);
        data        : std_logic_vector(31 downto 0);
    end record;
    
    type t_mem_resp_32 is record
        data        : std_logic_vector(31 downto 0);
        rack        : std_logic;
        rack_tag    : std_logic_vector(7 downto 0);
        dack_tag    : std_logic_vector(7 downto 0);
    end record;

    constant c_mem_req_32_init : t_mem_req_32 := (
        tag         => X"00",
        request     => '0',
        read_writen => '1',
        address     => (others => '0'),
        data        => X"00000000",
        byte_en     => "0000" );
     
    constant c_mem_resp_32_init : t_mem_resp_32 := (
        data        => X"00000000",
        rack        => '0',
        rack_tag    => X"00",
        dack_tag    => X"00" );
        
    type t_mem_req_32_array is array(natural range <>) of t_mem_req_32;
    type t_mem_resp_32_array is array(natural range <>) of t_mem_resp_32;

end package;

package body mem_bus_pkg is

end package body;
    