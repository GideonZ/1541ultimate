library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.core_pkg.all;

entity tb_decode_comb is

end entity;

architecture test of tb_decode_comb is
    type mem32_t is array(natural range <>) of std_logic_vector(31 downto 0);
    constant c_test_vector : mem32_t := (
x"30047073",
x"80010117",
x"7fc10113",
x"80010197",
x"7f418193",
x"00000517",
x"0c450513",
x"30551073",
x"34151073",
x"30001073",
x"30401073",
x"34401073",
x"32001073",
x"30601073",
x"b0001073",
x"b8001073",
x"b0201073",
x"b8201073",
x"00000093",
x"00000213",
x"00000293",
x"00000313",
x"00000393",
x"00010417",
x"da440413",
x"00010497",
x"f9c48493",
x"00001597",
x"e0858593",
x"80010617",
x"f8c60613",
x"80010697",
x"f8468693",
x"00d65c63",
x"0005a703",
x"00e62023",
x"00458593",
x"00460613",
x"fedff06f",
x"80010717",
x"f6470713",
x"80010797",
x"f6078793",
x"00f75863",
x"00072023",
x"00470713",
x"ff5ff06f",
x"00000513",
x"00000593",
x"060000ef",
x"34051073",
x"30047073",
x"10500073",
x"0000006f",
x"ff810113",
x"00812023",
x"00912223",
x"34202473",
x"02044663",
x"34102473",
x"00041483",
x"0034f493" );
    
    signal decoded_c       : t_decode_out;
    signal instruction     : std_logic_vector(31 downto 0);
    signal program_counter : std_logic_vector(31 downto 0);
    signal inst_valid      : std_logic;
begin
    process
    begin
        for i in c_test_vector'range loop
            program_counter <= std_logic_vector(to_unsigned(4 * i, 32));
            instruction <= c_test_vector(i);
            inst_valid  <= '1';
            wait for 10 ns;
        end loop;
        inst_valid <= '0';
        wait;
    end process;

    i_decode_comb: entity work.decode_comb
    port map (
        program_counter => program_counter,
        interrupt       => '0',
        instruction     => instruction,
        inst_valid      => inst_valid,
        decoded         => decoded_c
    );

end architecture;

