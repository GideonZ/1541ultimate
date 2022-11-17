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

    constant c_mem_bus_req_width    : natural := 71;
    function to_std_logic_vector(a: t_mem_req_32) return std_logic_vector;
    function to_mem_req(a: std_logic_vector(c_mem_bus_req_width-1 downto 0); valid: std_logic) return t_mem_req_32;
end package;

package body mem_bus_pkg is
    function to_std_logic_vector(a: t_mem_req_32) return std_logic_vector is
        variable ret : std_logic_vector(c_mem_bus_req_width-1 downto 0);
    begin
        ret := a.read_writen & a.byte_en & a.tag & std_logic_vector(a.address) & a.data;
        return ret;
    end function;

    function to_mem_req(a: std_logic_vector(c_mem_bus_req_width-1 downto 0); valid: std_logic) return t_mem_req_32 is
        variable ret : t_mem_req_32;
    begin
        ret.read_writen := a(70);
        ret.byte_en     := a(69 downto 66);
        ret.tag         := a(65 downto 58);
        ret.address     := unsigned(a(57 downto 32));
        ret.data        := a(31 downto 0);
        ret.request     := valid;
        return ret;
    end function;
end package body;
   