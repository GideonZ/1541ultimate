library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package mem_bus_pkg is

    type t_mem_req is record
        tag         : std_logic_vector(7 downto 0);
        request     : std_logic;
        read_writen : std_logic;
        address     : unsigned(25 downto 0);
        data        : std_logic_vector(7 downto 0);
    end record;
    
    type t_mem_resp is record
        data        : std_logic_vector(7 downto 0);
        rack        : std_logic;
        rack_tag    : std_logic_vector(7 downto 0);
        dack_tag    : std_logic_vector(7 downto 0);
    end record;

    constant c_mem_req_init : t_mem_req := (
        tag         => X"00",
        request     => '0',
        read_writen => '1',
        address     => (others => '0'),
        data        => X"00" );
     
    constant c_mem_resp_init : t_mem_resp := (
        data        => X"00",
        rack        => '0',
        rack_tag    => X"00",
        dack_tag    => X"00" );
        
    type t_mem_req_array is array(natural range <>) of t_mem_req;
    type t_mem_resp_array is array(natural range <>) of t_mem_resp;

    -- 8 bits memory bus with burst --
    type t_mem_burst_req is record
        request     : std_logic;
        read_writen : std_logic;
        address     : unsigned(25 downto 0);
        data        : std_logic_vector(7 downto 0);
        byte_en     : std_logic;
    end record;
    
    type t_mem_burst_resp is record
        data        : std_logic_vector(7 downto 0);
        rack        : std_logic;
        dack        : std_logic;
        dnext       : std_logic;
        blast       : std_logic;
		low_addr	: unsigned(2 downto 0);
    end record;

    constant c_mem_burst_req_init : t_mem_burst_req := (
        request     => '0',
        read_writen => '1',
        address     => (others => '0'),
        data        => X"00",
        byte_en     => '1' );

    -- 16 bits memory bus with burst --
    type t_mem_burst_16_req is record
        request     : std_logic;
        read_writen : std_logic;
        address     : unsigned(25 downto 0);
        data        : std_logic_vector(15 downto 0);
        byte_en     : std_logic_vector(1 downto 0);
    end record;
    
    type t_mem_burst_16_resp is record
        data        : std_logic_vector(15 downto 0);
        rack        : std_logic;
        dack        : std_logic;
        dnext       : std_logic;
        blast       : std_logic;
    end record;

    constant c_mem_burst_16_req_init : t_mem_burst_16_req := (
        request     => '0',
        read_writen => '1',
        address     => (others => '0'),
        data        => X"0000",
        byte_en     => "11" );


    -- 32 bits memory bus --
    type t_mem_32_req is record
        tag         : std_logic_vector(7 downto 0);
        request     : std_logic;
        read_writen : std_logic;
        address     : unsigned(25 downto 0);
        data        : std_logic_vector(31 downto 0);
        byte_en     : std_logic_vector(3 downto 0);
    end record;
    
    type t_mem_32_resp is record
        data        : std_logic_vector(31 downto 0);
        rack        : std_logic;
        rack_tag    : std_logic_vector(7 downto 0);
        dack_tag    : std_logic_vector(7 downto 0);
    end record;


end package;

package body mem_bus_pkg is

end package body;
    