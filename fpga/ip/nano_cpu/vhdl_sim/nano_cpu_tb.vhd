library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.nano_cpu_pkg.all;

entity nano_cpu_tb is
end entity;

architecture tb of nano_cpu_tb is

    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal ram_addr    : std_logic_vector(9 downto 0);
    signal ram_en      : std_logic;
    signal ram_we      : std_logic;
    signal ram_wdata   : std_logic_vector(15 downto 0);
    signal ram_rdata   : std_logic_vector(15 downto 0);
    signal io_addr     : unsigned(7 downto 0);
    signal io_write    : std_logic;
    signal io_wdata    : std_logic_vector(15 downto 0);
    signal io_rdata    : std_logic_vector(15 downto 0);

    type t_instructions is array(natural range <>) of std_logic_vector(15 downto 0);

--    constant c_load     : std_logic_vector(15 downto 11) := X"0" & '1'; -- load  
--    constant c_or       : std_logic_vector(15 downto 11) := X"1" & '1'; -- or
--    constant c_and      : std_logic_vector(15 downto 11) := X"2" & '1'; -- and
--    constant c_xor      : std_logic_vector(15 downto 11) := X"3" & '1'; -- xor
--    constant c_add      : std_logic_vector(15 downto 11) := X"4" & '1'; -- add
--    constant c_sub      : std_logic_vector(15 downto 11) := X"5" & '1'; -- sub
--    constant c_compare  : std_logic_vector(15 downto 11) := X"5" & '0'; -- sub
--    constant c_store    : std_logic_vector(15 downto 11) := X"8" & '0'; -- xxx
--    constant c_load_ind : std_logic_vector(15 downto 11) := X"8" & '1'; -- load
--    constant c_store_ind: std_logic_vector(15 downto 11) := X"9" & '0'; -- xxx
--    constant c_out      : std_logic_vector(15 downto 11) := X"A" & '0'; -- xxx
--    constant c_in       : std_logic_vector(15 downto 11) := X"A" & '1'; -- load
--
--    -- Specials    
--    constant c_return   : std_logic_vector(15 downto 11) := X"B" & '1'; -- xxx
--    constant c_branch   : std_logic_vector(15 downto 14) := "11";
--
--    -- Branches (bit 10..0) are address
--    constant c_br_eq    : std_logic_vector(13 downto 11) := "000"; -- zero
--    constant c_br_neq   : std_logic_vector(13 downto 11) := "001"; -- not zero
--    constant c_br_mi    : std_logic_vector(13 downto 11) := "010"; -- negative
--    constant c_br_pl    : std_logic_vector(13 downto 11) := "011"; -- not negative
--    constant c_br_always: std_logic_vector(13 downto 11) := "100"; -- always (jump)
--    constant c_br_call  : std_logic_vector(13 downto 11) := "101"; -- always (call)
--
    constant c_program : t_instructions := (
        X"0840", -- 00 -- load $40
        X"804E", -- 01 -- store $44
        X"4841", -- 02 -- add $41
        X"8045", -- 03 -- store $45
        X"0840", -- 04 -- load $40
        X"1841", -- 05 -- or $41
        X"8046", -- 06 -- store $46
        X"0840", -- 07 -- load $40
        X"2841", -- 08 -- and $41
        X"8047", -- 09 -- store $47
        X"8842", -- 0a -- load ($42)
        X"9043", -- 0b -- store ($43)
        X"0840", -- 0c -- load $40
        X"5041", -- 0d -- comp $41
        X"C000", -- 0e -- equal? => 0
        X"D800", -- 0f -- not negative? => 0
        X"A055", -- 10 -- out to $55
        X"6857", -- 11 -- in from $57
        X"E818", -- 12 -- call $18
        X"8048", -- 13 -- store $48
        X"E01A", -- 14 -- jump $1a
        X"0000", -- 15
        X"0000", -- 16
        X"0000", -- 17
        X"0844", -- 18 -- load $44
        X"B800", -- 19 -- return        
        X"6801", -- 1A -- in 1
        X"804F", -- 1B -- store in $4f
        X"A002", -- 1C -- out to $2
        X"5842", -- 1D -- sub 1
        X"C81C", -- 1E -- bne $1d
        X"E01F" -- 1F -- jump self
    );
    constant c_data : t_instructions(64 to 68) := (
        X"237A",
        X"59B2",
        X"0001",
        X"0049",
        X"FFFF" );
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_cpu: entity work.nano_cpu
    port map (
        clock       => clock,
        reset       => reset,
        
        -- instruction/data ram
        ram_addr    => ram_addr,
        ram_en      => ram_en,
        ram_we      => ram_we,
        ram_wdata   => ram_wdata,
        ram_rdata   => ram_rdata,
    
        -- i/o interface
        io_addr     => io_addr,
        io_write    => io_write,
        io_wdata    => io_wdata,
        io_rdata    => io_rdata );

    p_ram: process
        variable ram  : t_instructions(0 to 1023) := (others => (others => '1'));   
    begin
        ram(c_program'range) := c_program;
        ram(c_data'range) := c_data;
        while true loop
            wait until clock='1';
            if ram_en='1' then
                ram_rdata <= ram(to_integer(unsigned(ram_addr)));
                if ram_we='1' then
                    ram(to_integer(unsigned(ram_addr))) := ram_wdata;
                end if;
            end if;
        end loop;
    end process;
    
    io_rdata <= X"0006";
end tb;
