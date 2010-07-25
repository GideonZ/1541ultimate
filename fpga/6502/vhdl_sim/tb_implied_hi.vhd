library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

entity tb_implied_hi is
end tb_implied_hi;

architecture tb of tb_implied_hi is
    signal inst     : std_logic_vector(7 downto 0);
    signal n_in     : std_logic := 'Z';
    signal z_in     : std_logic := 'Z';
    signal d_in     : std_logic := 'Z';
    signal v_in     : std_logic := 'Z';    
    signal reg_a    : std_logic_vector(7 downto 0) := X"11";
    signal reg_x    : std_logic_vector(7 downto 0) := X"01";
    signal reg_y    : std_logic_vector(7 downto 0) := X"FF";
    signal reg_s    : std_logic_vector(7 downto 0) := X"44";
    signal n_out    : std_logic;
    signal z_out    : std_logic;
    signal d_out    : std_logic;
    signal v_out    : std_logic;
    signal set_a    : std_logic;
    signal set_x    : std_logic;
    signal set_y    : std_logic;
    signal set_s    : std_logic;
    signal data_out : std_logic_vector(7 downto 0);

    signal opcode   : string(1 to 3);

    type t_implied_opcode is array(0 to 15) of string(1 to 3);
    constant implied_opcodes : t_implied_opcode := (
        "DEY", "TAY", "INY", "INX",
        "TXA", "TAX", "DEX", "NOP",
        "TYA", "CLV", "CLD", "SED",
        "TXS", "TSX", "---", "---" );

begin

    mut: entity work.implied_hi
    port map (
        inst      => inst,
                             
        n_in      => n_in,
        z_in      => z_in,
        d_in      => d_in,
        v_in      => v_in,
                             
        reg_a     => reg_a,
        reg_x     => reg_x,
        reg_y     => reg_y,
        reg_s     => reg_s,
                             
        n_out     => n_out,
        z_out     => z_out,
        d_out     => d_out,
        v_out     => v_out,
                             
        set_a     => set_a,
        set_x     => set_x,
        set_y     => set_y,
        set_s     => set_s,
                             
        data_out  => data_out );

    test: process
        variable inst_thumb : std_logic_vector(3 downto 0);
    begin
        for i in 0 to 15 loop
            inst_thumb := conv_std_logic_vector(i, 4);
            inst <= '1' & inst_thumb(1 downto 0) & inst_thumb(3) & "10" & inst_thumb(2) & '0';
            opcode <= implied_opcodes(i);
            wait for 1 us;
        end loop;
        wait;
    end process;
end tb;

