library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package dma_bus_pkg is

    type t_dma_req is record
        request     : std_logic;
        read_writen : std_logic;
        address     : unsigned(15 downto 0);
        data        : std_logic_vector(7 downto 0);
    end record;
    
    type t_dma_resp is record
        data        : std_logic_vector(7 downto 0);
        rack        : std_logic;
        dack        : std_logic;
    end record;

    constant c_dma_req_init : t_dma_req := (
        request     => '0',
        read_writen => '1',
        address     => (others => '0'),
        data        => X"00" );
     
    constant c_dma_resp_init : t_dma_resp := (
        data        => X"00",
        rack        => '0',
        dack        => '0' );
        
    type t_dma_req_array is array(natural range <>) of t_dma_req;
    type t_dma_resp_array is array(natural range <>) of t_dma_resp;

end package;

package body dma_bus_pkg is

end package body;
    