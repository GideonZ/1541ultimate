library ieee;
use ieee.std_logic_1164.all;

entity G is
port (GSR : in std_logic);
end entity;
architecture mine of G is
    signal GSRNET : std_logic;
begin
    GSRNET <= GSR;
end architecture;

--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity P is
port (PUR : in std_logic);
end entity;
architecture mine of P is
    signal PURNET : std_logic;
begin
    PURNET <= PUR;
end architecture;

--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library ecp5u;
use ecp5u.components.all;

entity tb_mul_add_check is
end entity;

architecture tb of tb_mul_add_check is
    signal clock    : std_logic := '0';
    signal my_net : std_logic;

    signal ram      : std_logic_vector(7 downto 0);
    signal a_1      : std_logic_vector(17 downto 0);
    signal clear    : std_logic;
    signal result   : std_logic_vector(31 downto 1);
    signal result_ref   : signed(31 downto 0);
    signal a            : signed(17 downto 0);
    signal b            : signed(8 downto 0);
begin
    PUR_INST: entity work.P port map (PUR => my_net);
    GSR_INST: entity work.G port map (GSR => my_net);
    my_net <= '0', '1' after 50 ns;
    clock <= not clock after 10 ns;
    
    i_dut: entity work.mul_add_uniq_0
    port map(
        ram          => ram,
        a_1          => a_1,
        result       => result,
        clear        => clear,
        AUDIO_MCLK_c => clock
    );

    i_ref: entity work.mul_add
    port map(
        clock  => clock,
        clear  => clear,
        a      => a,
        b      => b,
        result => result_ref
    );
    
    a <= signed(a_1);
    b <= '0' & signed(ram);

    process
    begin
        ram <= X"00";
        a_1 <= (others => '0');
        clear <= '0';
        wait until clock = '1';
        wait until clock = '1';
        wait until clock = '1';
        wait until clock = '1';
        wait until clock = '1';
        wait until clock = '1';
        wait until clock = '1';
        wait until clock = '1';
        clear <= '1';
        wait until clock = '1';
        clear <= '0';
        for i in 1 to 16 loop
            ram <= std_logic_vector(to_unsigned(i, 8));
            a_1 <= std_logic_vector(to_unsigned(i, 18));
            wait until clock = '1';
        end loop;
    end process;
end architecture;
