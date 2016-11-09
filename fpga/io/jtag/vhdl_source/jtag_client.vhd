--------------------------------------------------------------------------------
-- Entity: jtag_client
-- Date:2016-11-08  
-- Author: Gideon     
--
-- Description: Client for Virtual JTAG module
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity jtag_client is
generic (
    g_write_width  : natural := 8;
    g_sample_width : natural := 40 );
port (
    sample_vector   : in  std_logic_vector(g_sample_width-1 downto 0);
    write_vector    : out std_logic_vector(g_write_width-1 downto 0) );
end entity;

architecture arch of jtag_client is
    component virtual_jtag is
        port (
            tdi                : out std_logic;                                       -- tdi
            tdo                : in  std_logic                    := 'X';             -- tdo
            ir_in              : out std_logic_vector(3 downto 0);                    -- ir_in
            ir_out             : in  std_logic_vector(3 downto 0) := (others => 'X'); -- ir_out
            virtual_state_cdr  : out std_logic;                                       -- virtual_state_cdr
            virtual_state_sdr  : out std_logic;                                       -- virtual_state_sdr
            virtual_state_e1dr : out std_logic;                                       -- virtual_state_e1dr
            virtual_state_pdr  : out std_logic;                                       -- virtual_state_pdr
            virtual_state_e2dr : out std_logic;                                       -- virtual_state_e2dr
            virtual_state_udr  : out std_logic;                                       -- virtual_state_udr
            virtual_state_cir  : out std_logic;                                       -- virtual_state_cir
            virtual_state_uir  : out std_logic;                                       -- virtual_state_uir
            tck                : out std_logic                                        -- clk
        );
    end component virtual_jtag;

    constant c_rom            : std_logic_vector(31 downto 0) := X"DEAD1541";
    signal shiftreg_sample    : std_logic_vector(g_sample_width-1 downto 0);
    signal shiftreg_write     : std_logic_vector(g_write_width-1 downto 0);
    signal bypass_reg         : std_logic;
    signal bit_count          : unsigned(4 downto 0) := (others => '0');    
    signal tdi                : std_logic;                                       -- tdi
    signal tdo                : std_logic                    := 'X';             -- tdo
    signal ir_in              : std_logic_vector(3 downto 0);                    -- ir_in
    signal ir_out             : std_logic_vector(3 downto 0) := (others => 'X'); -- ir_out
    signal virtual_state_cdr  : std_logic;                                       -- virtual_state_cdr
    signal virtual_state_sdr  : std_logic;                                       -- virtual_state_sdr
    signal virtual_state_e1dr : std_logic;                                       -- virtual_state_e1dr
    signal virtual_state_pdr  : std_logic;                                       -- virtual_state_pdr
    signal virtual_state_e2dr : std_logic;                                       -- virtual_state_e2dr
    signal virtual_state_udr  : std_logic;                                       -- virtual_state_udr
    signal virtual_state_cir  : std_logic;                                       -- virtual_state_cir
    signal virtual_state_uir  : std_logic;                                       -- virtual_state_uir
    signal tck                : std_logic;                                       -- clk

begin
    i_vj: virtual_jtag
    port map (
        tdi                => tdi,
        tdo                => tdo,
        ir_in              => ir_in,
        ir_out             => ir_out,
        virtual_state_cdr  => virtual_state_cdr,
        virtual_state_sdr  => virtual_state_sdr,
        virtual_state_e1dr => virtual_state_e1dr,
        virtual_state_pdr  => virtual_state_pdr,
        virtual_state_e2dr => virtual_state_e2dr,
        virtual_state_udr  => virtual_state_udr,
        virtual_state_cir  => virtual_state_cir,
        virtual_state_uir  => virtual_state_uir,
        tck                => tck
    );
    
    process(tck)
    begin
        if rising_edge(tck) then
            if virtual_state_cdr = '1' then
                shiftreg_write  <= (others => '0');
                shiftreg_sample <= sample_vector;
                bit_count <= (others => '0');
            end if;                
            if virtual_state_sdr = '1' then
                bypass_reg <= tdi;
                shiftreg_sample <= tdi & shiftreg_sample(shiftreg_sample'high downto 1);
                shiftreg_write <= tdi & shiftreg_write(shiftreg_write'high downto 1);
                bit_count <= bit_count + 1;
            end if;
            if virtual_state_udr = '1' then
                case ir_in is
                when X"2" =>
                    write_vector <= shiftreg_write;
                when others =>
                    null;
                end case;
            end if;
        end if;
        
        if falling_edge(tck) then
            case ir_in is
            when X"0" =>
                tdo <= c_rom(to_integer(bit_count));
            when X"1" =>
                tdo <= shiftreg_sample(0);
            when X"2" =>
                tdo <= shiftreg_write(0);
            when others =>
                tdo <= bypass_reg;
            end case;            
        end if;
    end process;

end arch;
