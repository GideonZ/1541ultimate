--------------------------------------------------------------------------------
-- tb_magic_desk_plus -- what the Magic Desk Plus registers have to do.
--
-- Drives all_carts_v5 as the C64 would: writes to DE00, DE01 and DE03, then
-- reads back the memory address the cartridge logic produces for the DF00
-- window and for the ROM window. Checks the four behaviours the format
-- specifies, plus the two the Ultimate has to get right for it to be usable:
-- the window has to stay served with the ROM switched off, and the address has
-- to stay inside the region the REU and GeoRAM share.
--
-- Runs against a v3.15 checkout:
--   ghdl -r --std=02 tb_magic_desk_plus
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.slot_bus_pkg.all;
use work.io_bus_pkg.all;

entity tb_magic_desk_plus is
end entity;

architecture arch of tb_magic_desk_plus is
    constant c_magic_desk_p : std_logic_vector(4 downto 0) := "10010";
    constant c_georam_base  : std_logic_vector(27 downto 0) := X"1000000";

    signal clock       : std_logic := '0';
    signal reset       : std_logic := '1';
    signal stopped     : boolean := false;

    signal slot_req    : t_slot_req := c_slot_req_init;
    signal slot_resp   : t_slot_resp;
    signal io_req      : t_io_req := c_io_req_init;
    signal io_resp     : t_io_resp;

    signal cart_logic  : std_logic_vector(4 downto 0) := c_magic_desk_p;
    signal cart_var    : std_logic_vector(2 downto 0) := "000";

    signal serve_rom   : std_logic;
    signal serve_io1   : std_logic;
    signal serve_io2   : std_logic;
    signal serve_128   : std_logic;
    signal serve_vic   : std_logic;
    signal serve_en    : std_logic;
    signal allow_write : std_logic;
    signal mem_addr    : unsigned(25 downto 0);
    signal exrom_n     : std_logic;
    signal game_n      : std_logic;
    signal irq_n       : std_logic;
    signal nmi_n       : std_logic;
    signal cart_led    : std_logic;
    signal cart_active : std_logic;
    signal freezer_ena : std_logic;
    signal unfreeze    : std_logic;

    signal rst_in      : std_logic := '0';
    signal errors      : integer := 0;
begin
    clock <= not clock after 10 ns when not stopped;
    reset <= '1', '0' after 200 ns;

    i_dut: entity work.all_carts_v5
    generic map (
        g_register_addr => false,
        g_eeprom        => false,
        g_max_cart_bits => 22,
        g_georam_base   => c_georam_base )
    port map (
        clock          => clock,
        reset          => reset,
        io_req_eeprom  => io_req,
        io_resp_eeprom => io_resp,
        RST_in         => rst_in,
        c64_reset      => '0',
        kernal_enable  => '0',
        kernal_area    => '0',
        freeze_trig    => '0',
        freeze_act     => '0',
        freezer_ena    => freezer_ena,
        unfreeze       => unfreeze,
        cart_active    => cart_active,
        cart_kill      => '0',
        cart_logic     => cart_logic,
        cart_variant   => cart_var,
        cart_force     => '0',
        slot_req       => slot_req,
        slot_resp      => slot_resp,
        epyx_timeout   => '0',
        serve_enable   => serve_en,
        serve_vic      => serve_vic,
        serve_128      => serve_128,
        serve_rom      => serve_rom,
        serve_io1      => serve_io1,
        serve_io2      => serve_io2,
        allow_write    => allow_write,
        mem_req        => '0',
        mem_addr       => mem_addr,
        phi2           => '0',
        irq_n          => irq_n,
        nmi_n          => nmi_n,
        exrom_n        => exrom_n,
        game_n         => game_n,
        CART_LEDn      => cart_led,
        size_ctrl      => "001" );

    main: process
        variable n_err : integer := 0;

        procedure tick is
        begin
            wait until clock = '1';
        end procedure;

        -- A write into the IO1 page, the way the C64 reaches DE00..DEFF.
        procedure io_wr(addr : in std_logic_vector(15 downto 0);
                        data : in std_logic_vector(7 downto 0)) is
        begin
            tick;
            slot_req.io_address <= unsigned(addr);
            slot_req.data       <= data;
            slot_req.io_write   <= '1';
            tick;
            slot_req.io_write   <= '0';
            tick;
        end procedure;

        -- Point the bus at an address and let the logic settle, so mem_addr
        -- shows what the cartridge would drive for that access.
        procedure bus_at(addr : in std_logic_vector(15 downto 0)) is
        begin
            slot_req.bus_address <= unsigned(addr);
            tick; tick;
            wait for 1 ns;
        end procedure;

        procedure check(cond : in boolean; msg : in string) is
        begin
            if not cond then
                n_err := n_err + 1;
                report "FAIL: " & msg severity error;
            end if;
        end procedure;

        -- cart_variant is sampled only while the cartridge is in reset, the
        -- same as on the machine, where the firmware writes the type and then
        -- resets the cart. Changing it without this does nothing.
        procedure cart_reset is
        begin
            tick;
            rst_in <= '1';
            tick; tick;
            rst_in <= '0';
            tick; tick;
        end procedure;
    begin
        wait until reset = '0';
        tick; tick;

        ------------------------------------------------------------------
        report "power-up state" severity note;
        ------------------------------------------------------------------
        -- The format says the EEPROM is selected and the first SRAM portion
        -- is the default. Address bit 17 carries the SRAM select, bit 16 the
        -- portion, so both are zero here.
        bus_at(X"DF00");
        check(serve_io2 = '1', "the DF00 window must be served");
        check(mem_addr(17) = '0', "the EEPROM must be selected at power-up");
        check(mem_addr(16) = '0', "the first SRAM portion must be the default");
        check(allow_write = '1', "the window must be writable");

        ------------------------------------------------------------------
        report "DE00: bank bits 0..6 and the disable bit" severity note;
        ------------------------------------------------------------------
        io_wr(X"DE00", X"00");
        bus_at(X"8000");
        check(exrom_n = '0', "bank 0 without bit 7 must map the ROM");
        check(serve_rom = '1', "the ROM window must be served");

        io_wr(X"DE00", X"7F"); -- highest bank, ROM still on
        bus_at(X"8000");
        check(exrom_n = '0', "bit 7 clear must leave the ROM mapped");
        check(mem_addr(20 downto 14) = "1111111", "bank 127 must reach the address");

        io_wr(X"DE00", X"40"); -- bank 64: the bit a plain Magic Desk cannot reach
        bus_at(X"8000");
        check(mem_addr(20) = '1', "bit 6 must be a bank bit, not ignored");

        io_wr(X"DE00", X"80"); -- disable
        bus_at(X"8000");
        check(exrom_n = '1', "bit 7 must switch the ROM off");
        bus_at(X"DF00");
        check(serve_io2 = '1', "the window must survive the ROM being off");

        io_wr(X"DE00", X"00"); -- back on

        ------------------------------------------------------------------
        report "DE01: the page register" severity note;
        ------------------------------------------------------------------
        io_wr(X"DE03", X"20"); -- SRAM on, first portion
        io_wr(X"DE01", X"00");
        bus_at(X"DF00");
        check(mem_addr(15 downto 8) = X"00", "page 0 must select the first page");

        io_wr(X"DE01", X"A5");
        bus_at(X"DF00");
        check(mem_addr(15 downto 8) = X"A5", "the page must reach address bits 15..8");

        io_wr(X"DE01", X"FF");
        bus_at(X"DF80");
        check(mem_addr(15 downto 8) = X"FF", "page 255 must be reachable");
        check(mem_addr(7 downto 0) = X"80", "the offset in the window comes from the bus");

        ------------------------------------------------------------------
        report "DE03: portion and SRAM/EEPROM select" severity note;
        ------------------------------------------------------------------
        io_wr(X"DE03", X"20"); -- bit 5 = SRAM, bit 0 = 0
        bus_at(X"DF00");
        check(mem_addr(17) = '1', "bit 5 must select the SRAM");
        check(mem_addr(16) = '0', "bit 0 clear must select the first portion");

        io_wr(X"DE03", X"21"); -- bit 0 = second portion
        bus_at(X"DF00");
        check(mem_addr(16) = '1', "bit 0 set must select the second portion");

        io_wr(X"DE03", X"01"); -- bit 5 clear: back to the EEPROM
        bus_at(X"DF00");
        check(mem_addr(17) = '0', "bit 5 clear must select the EEPROM");

        ------------------------------------------------------------------
        report "the EEPROM answers only inside its own size" severity note;
        ------------------------------------------------------------------
        -- variant 0 is the 8K EEPROM: 32 pages, mask 0x1F.
        cart_var <= "000";
        cart_reset;
        io_wr(X"DE03", X"00");
        io_wr(X"DE01", X"FF");
        bus_at(X"DF00");
        check(mem_addr(15 downto 8) = X"1F", "an 8K EEPROM must mask the page to 0x1F");

        -- variant 1 is the 32K EEPROM: 128 pages, mask 0x7F.
        cart_var <= "001";
        cart_reset;
        io_wr(X"DE03", X"00");
        io_wr(X"DE01", X"FF");
        bus_at(X"DF00");
        check(mem_addr(15 downto 8) = X"7F", "a 32K EEPROM must mask the page to 0x7F");

        -- The SRAM is not masked.
        io_wr(X"DE03", X"20");
        io_wr(X"DE01", X"FF");
        bus_at(X"DF00");
        check(mem_addr(15 downto 8) = X"FF", "the SRAM must use all 256 pages");

        ------------------------------------------------------------------
        report "everything stays inside the shared region" severity note;
        ------------------------------------------------------------------
        io_wr(X"DE03", X"21");
        io_wr(X"DE01", X"FF");
        bus_at(X"DFFF");
        -- g_georam_base is 0x1000000, so bit 24 belongs to the region itself.
        -- What has to stay clear is everything between it and the 256K the
        -- SRAM and EEPROM occupy.
        check(mem_addr(25 downto 24) = "01", "the window must sit in the shared region");
        check(mem_addr(23 downto 18) = "000000",
              "the window must not reach past the first 256K of the region");

        ------------------------------------------------------------------
        errors <= n_err;
        if n_err = 0 then
            report "RESULT: all checks passed" severity note;
        else
            report "RESULT: " & integer'image(n_err) & " check(s) failed" severity note;
        end if;
        stopped <= true;
        wait;
    end process;
end arch;
