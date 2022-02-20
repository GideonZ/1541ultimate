library ieee;
use ieee.std_logic_1164.all;

entity dpram_8x16 is
port (
    CLKA  : in std_logic;
    SSRA  : in std_logic;
    ENA   : in std_logic;
    WEA   : in std_logic;
    ADDRA : in std_logic_vector(10 downto 0);
    DIA   : in std_logic_vector(7 downto 0);
    DOA   : out std_logic_vector(7 downto 0);

    CLKB  : in std_logic;
    SSRB  : in std_logic;
    ENB   : in std_logic;
    WEB   : in std_logic;
    ADDRB : in std_logic_vector(9 downto 0);
    DIB   : in std_logic_vector(15 downto 0);
    DOB   : out std_logic_vector(15 downto 0)
);
end entity;

