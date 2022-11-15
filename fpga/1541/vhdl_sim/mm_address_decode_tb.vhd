library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mm_address_decode_tb is
end entity;

architecture tb of mm_address_decode_tb is
    signal cpu_addr         : unsigned(15 downto 0) := X"0000";
    type t_drive_mode_bundle is record
        -- address decode
        via1_sel        : std_logic;
        via2_sel        : std_logic;
        wd_sel          : std_logic;
        cia_sel         : std_logic;
        open_sel        : std_logic;
    end record;

    type t_drive_mode_bundles is array(natural range <>) of t_drive_mode_bundle;
    signal m    : t_drive_mode_bundles(0 to 2);
    signal extra_ram    : std_logic := '0';
begin
    process
    begin
        for i in 0 to 255 loop
            wait for 10 ns;
            cpu_addr <= cpu_addr + X"0100";
        end loop;
        extra_ram <= not extra_ram;
    end process;


    -- Address decoding 1541
    m(0).via1_sel <= '1' when cpu_addr(12 downto 10)="110" and cpu_addr(15)='0' and (extra_ram='0' or cpu_addr(14 downto 13)="00") else '0';
    m(0).via2_sel <= '1' when cpu_addr(12 downto 10)="111" and cpu_addr(15)='0' and (extra_ram='0' or cpu_addr(14 downto 13)="00") else '0';
    m(0).open_sel <= '1' when (cpu_addr(12) xor cpu_addr(11)) = '1' and cpu_addr(15) = '0' and extra_ram = '0' else '0';
    m(0).cia_sel  <= '0';
    m(0).wd_sel   <= '0';
    
    -- Address decoding 1571
    m(1).via1_sel <= '1' when cpu_addr(15 downto 10)="000110" else '0';
    m(1).via2_sel <= '1' when cpu_addr(15 downto 10)="000111" else '0';
    m(1).open_sel <= '1' when (cpu_addr(15 downto 11)="00001" or cpu_addr(15 downto 11) = "00010") and extra_ram = '0' else '0';
    m(1).cia_sel  <= '1' when cpu_addr(15 downto 14)="01" else '0';
    m(1).wd_sel   <= '1' when cpu_addr(15 downto 13)="001" else '0';

    -- Address decoding 1581
    m(2).via1_sel <= '0';
    m(2).via2_sel <= '0';
    m(2).open_sel <= '1' when cpu_addr(15 downto 13)="001" and extra_ram = '0' else '0'; -- 2000
    m(2).cia_sel  <= '1' when cpu_addr(15 downto 13)="010" else '0'; -- 4000
    m(2).wd_sel   <= '1' when cpu_addr(15 downto 13)="011" else '0'; -- 6000

end architecture;