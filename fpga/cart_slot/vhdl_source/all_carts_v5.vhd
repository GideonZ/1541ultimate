library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.slot_bus_pkg.all;
use work.io_bus_pkg.all;

entity all_carts_v5 is
generic (
    g_register_addr : boolean := false;
    g_eeprom        : boolean := true;
    g_kernal_base   : std_logic_vector(27 downto 0) := X"0EC8000"; -- multiple of 32K
    g_rom_base      : std_logic_vector(27 downto 0) := X"0F00000"; -- multiple of 1M
    g_georam_base   : std_logic_vector(27 downto 0) := X"1000000"; -- Shared with reu
    g_ram_base      : std_logic_vector(27 downto 0) := X"0EF0000" ); -- multiple of 64K

port (
    clock           : in  std_logic;
    reset           : in  std_logic;

    io_req_eeprom   : in  t_io_req;
    io_resp_eeprom  : out t_io_resp := c_io_resp_init;
    
    RST_in          : in  std_logic;
    c64_reset       : in  std_logic;
    
    kernal_enable   : in  std_logic;
    kernal_area     : in  std_logic;
    freeze_trig     : in  std_logic; -- goes '1' when the button has been pressed and we're waiting to enter the freezer
    freeze_act      : in  std_logic; -- goes '1' when we need to switch in the cartridge for freeze mode
    freezer_ena     : out std_logic; -- is '1' for carts that support a freezer
    unfreeze        : out std_logic; -- indicates the freeze logic to switch back to non-freeze mode.
    cart_active     : out std_logic; -- indicates that the cartridge is active

    cart_kill       : in  std_logic;
    cart_logic      : in  std_logic_vector(4 downto 0);   -- 1 out of 32 logic emulations
    cart_variant    : in  std_logic_vector(2 downto 0);   -- max 8 variants for one emulation class
    cart_force      : in  std_logic;

    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp := c_slot_resp_init;

    epyx_timeout    : in  std_logic;
    serve_enable    : out std_logic; -- enables fetching bus address PHI2=1
    serve_vic       : out std_logic; -- enables doing so for PHI2=0
    serve_128       : out std_logic; -- 8000-FFFF
    serve_rom       : out std_logic; -- ROML or ROMH
    serve_io1       : out std_logic; -- IO1n
    serve_io2       : out std_logic; -- IO2n
    allow_write     : out std_logic;

    mem_req         : in  std_logic; -- if '1', the address shouldn't change
    mem_addr        : out unsigned(25 downto 0);   

    phi2            : in  std_logic;
    irq_n           : out std_logic;
    nmi_n           : out std_logic;
    exrom_n         : out std_logic;
    game_n          : out std_logic;

    CART_LEDn       : out std_logic;
    size_ctrl       : in  std_logic_vector(2 downto 0) := "001" );

end all_carts_v5;    

architecture gideon of all_carts_v5 is
    signal reset_in     : std_logic;

    signal rom_mode     : std_logic_vector(14 downto 13) := "11";
    signal bank_bits    : std_logic_vector(19 downto 13);
    signal ram_bank     : std_logic_vector(15 downto 13) := "000";
    signal mode_bits    : std_logic_vector(2 downto 0);
    signal ef_write     : std_logic := '0';
    signal georam_bank  : std_logic_vector(15 downto 0);
    
    signal freeze_act_d : std_logic;
    signal cart_en      : std_logic;
    signal do_io2       : std_logic;
    signal allow_bank   : std_logic;
    signal hold_nmi     : std_logic;
    signal mem_addr_i   : std_logic_vector(27 downto 0);
    signal mem_addr_c   : std_logic_vector(27 downto 0);
    signal rom_addr     : std_logic_vector(27 downto 0);
    signal ram_addr     : std_logic_vector(27 downto 0);

    signal cart_logic_d : std_logic_vector(cart_logic'range) := (others => '0');
    signal variant      : std_logic_vector(cart_variant'range) := (others => '0');
            
    signal ee_clk, ee_sel       : std_logic;
    signal ee_rdata, ee_wdata   : std_logic;

    -- Ultra Simple
    constant c_none         : std_logic_vector(4 downto 0) := "00000";
    constant c_normal       : std_logic_vector(4 downto 0) := "00001";
    constant c_epyx         : std_logic_vector(4 downto 0) := "00010";
    constant c_128          : std_logic_vector(4 downto 0) := "00011";
    constant c_westermann   : std_logic_vector(4 downto 0) := "00100"; -- also blackbox v4 with variant
    constant c_sbasic       : std_logic_vector(4 downto 0) := "00101";
    constant c_bbasic       : std_logic_vector(4 downto 0) := "00110";
    constant c_blackbox_v3  : std_logic_vector(4 downto 0) := "00111";

    -- Simple banking
    constant c_ocean_8K     : std_logic_vector(4 downto 0) := "01000";
    constant c_ocean_16K    : std_logic_vector(4 downto 0) := "01001";
    constant c_system3      : std_logic_vector(4 downto 0) := "01010";
    constant c_supergames   : std_logic_vector(4 downto 0) := "01011";
    constant c_blackbox_v8  : std_logic_vector(4 downto 0) := "01100";
    constant c_zaxxon       : std_logic_vector(4 downto 0) := "01101";
    constant c_blackbox_v9  : std_logic_vector(4 downto 0) := "01110";

    -- Simple bankers with RAM
    constant c_pagefox      : std_logic_vector(4 downto 0) := "10000";
    constant c_easy_flash   : std_logic_vector(4 downto 0) := "10001";

    -- Freezers
    constant c_fc           : std_logic_vector(4 downto 0) := "11000";
    constant c_fc3          : std_logic_vector(4 downto 0) := "11001";
    constant c_ss5          : std_logic_vector(4 downto 0) := "11010";
    constant c_action       : std_logic_vector(4 downto 0) := "11011";
    constant c_kcs          : std_logic_vector(4 downto 0) := "11100";

    -- Exotics
    constant c_georam       : std_logic_vector(4 downto 0) := "11111";
    
    constant c_serve_rom_rr : std_logic_vector(0 to 7) := "11011111";
    constant c_serve_io_rr  : std_logic_vector(0 to 7) := "10101111";
    
    type t_address_select is ( ROM, RAM, GEO );
    signal addr_map         : t_address_select;

    -- alias
    signal slot_addr        : std_logic_vector(15 downto 0);
    signal slot_rwn         : std_logic;
    signal io_read          : std_logic;
    signal io_write         : std_logic;
    signal io_addr          : std_logic_vector(8 downto 0);
    signal io_wdata         : std_logic_vector(7 downto 0);
    signal georam_mask      : std_logic_vector(15 downto 0);

begin
    with size_ctrl select georam_mask <=
        "0000000111111111" when "000",
        "0000001111111111" when "001",
        "0000011111111111" when "010",
        "0000111111111111" when "011",
        "0001111111111111" when "100",
        "0011111111111111" when "101",
        "0111111111111111" when "110",
        "1111111111111111" when others;


    serve_enable <= cart_en or kernal_enable;
    cart_active  <= cart_en;

    slot_addr <= std_logic_vector(slot_req.bus_address);
    slot_rwn  <= slot_req.bus_rwn;
    io_write  <= slot_req.io_write;
    io_read   <= slot_req.io_read;
    io_addr   <= std_logic_vector(slot_req.io_address(8 downto 0));
    io_wdata  <= slot_req.data;
    
    process(clock)
        variable v_addr4 : std_logic_vector(3 downto 0);
    begin
        if rising_edge(clock) then
            reset_in     <= reset or RST_in or c64_reset;
            freeze_act_d <= freeze_act;
            unfreeze     <= '0';

            -- control register
            if freeze_act='1' and freeze_act_d='0' then
                bank_bits  <= (others => '0');
                ram_bank   <= (others => '0');
                mode_bits  <= (others => '0');
                cart_en    <= '1';
                hold_nmi   <= '1';

            -- activate change of mode, when:
            elsif reset_in='1' or cart_force = '1' then
                cart_logic_d <= cart_logic;
                variant      <= cart_variant;
                mode_bits    <= (others => '0');
                bank_bits    <= (others => '0');
                ram_bank     <= (others => '0');
                georam_bank  <= (others => '0');
                ef_write     <= '0';
                allow_bank   <= '0';
                do_io2       <= '1';
                cart_en      <= reset_in; -- When reset, cart_en = 1, when forcing cartridge mode, it is forced to OFF! (Freezer will enable it)
                hold_nmi     <= '0';
                ee_clk       <= '0';
                ee_sel       <= '0';
                ee_wdata     <= '0';
            end if;
                            
            -- Default, everything is off.
            serve_128 <= '0';
            serve_rom <= '0';
            serve_io1 <= '0';
            serve_io2 <= '0';
            serve_vic <= '0';
            irq_n     <= '1';
            nmi_n     <= '1';
            game_n    <= '1';
            exrom_n   <= '1';
            rom_mode  <= "01"; -- No banking, All within 16K
            freezer_ena <= '0';
                    
            case cart_logic_d is
            when c_none =>
                cart_en <= '0';
                -- other defaults are OK

            -- ULTRA SIMPLE CARTS, NO BANKING, NO RAM
            when c_normal =>
                if io_write='1' and io_addr(8 downto 0) = "111111111" then -- DFFF
                    if cart_en='1' and io_wdata(7 downto 6) = "01" then
                        cart_en <= '0'; -- permanent off
                    end if;
                end if;
                game_n    <= variant(1);
                exrom_n   <= variant(0);
                serve_rom <= '1';
                serve_vic <= variant(2);
                
            when c_128 =>
                serve_128 <= '1'; -- 8000-FFFF
                serve_vic <= '1';
                serve_io1 <= variant(0);
                serve_io2 <= variant(1);
                rom_mode  <= "11"; -- Banking here is 32K!
                if variant(2)='1' then
                    if io_write='1' and io_addr(8 downto 0)="101111111" then -- DF7F
                        bank_bits(19 downto 15) <= io_wdata(4 downto 0);
                    end if;
                    if io_write='1' and io_addr(8 downto 0)="101111110" then -- DF7E
                        ram_bank <= io_wdata(2 downto 0);
                    end if;
                end if;

            when c_epyx =>
                game_n    <= '1';
                exrom_n   <= epyx_timeout;
                serve_rom <= '1';
                serve_io2 <= '1'; -- rom visible df00-dfff

            when c_westermann => -- 16K
                -- Variant bit 0: enable turning on by reading DExx
                -- Variant bit 1: enable keeping 8000-9FFF always enabled
                -- 
                -- Variant 0: Westermann. Complete off when reading DFxx
                -- Variant 1: Blackbox V4 (16K or off). Reading DFxx = off, Reading DExx = on
                -- Variant 2: Westermann. Only upper cartridge half turned off when reading from DFxx
                if io_read='1' and io_addr(8)='1' then
                    mode_bits(0) <= '1';
                elsif io_read='1' and io_addr(8)='0' and variant(0)='1' then -- read IO1
                    mode_bits(0) <= '0';
                end if;
                game_n    <= mode_bits(0);
                if variant(1)='1' then
                    exrom_n   <= '0';
                else
                    exrom_n   <= mode_bits(0);
                end if;
                serve_rom <= '1';

            when c_sbasic => -- 16K, upper 8k enabled by writing to DExx
                             -- and disabled by reading
                if io_write='1' and io_addr(8)='0' then
                    mode_bits(0) <= '1';
                elsif io_read='1' and io_addr(8)='0' then
                    mode_bits(0) <= '0';
                end if;
                game_n    <= not mode_bits(0);
                exrom_n   <= '0';
                serve_rom <= '1';

            when c_bbasic => -- Write IO1 = off, Read IO1 = ON
                if io_write='1' and io_addr(8)='0' then
                    mode_bits(0) <= '0';
                elsif io_read='1' and io_addr(8)='0' then
                    mode_bits(0) <= '1';
                end if;
                if mode_bits(0)='1' then
                   game_n    <= '0';
                   exrom_n   <= '0';
                -- Dynamic mode, Ultimax in ranges 8000, A000 and E000.  How about writes?
                elsif slot_addr(15)='1' and not(slot_addr(14 downto 13) = "10") then -- 100x 101x 111x => 8000, A000, E000
                   game_n    <= '0';
                   exrom_n   <= '1';
                else -- Off
                   game_n    <= '1';
                   exrom_n   <= '1';
                end if;
                serve_rom <= '1';
                serve_io1 <= '1';
                rom_mode  <= "11"; -- 32K! The ROMs shall be placed in memory at the right location by the software.
                -- Note that the CRT files are usually wrong; they list 3 banks of 8K, all at $8000, which is incorrect.

            when c_blackbox_v3 =>
                if io_write='1' and io_addr(8)='0' then -- write IO1 => disable
                    mode_bits(0) <= '1';
                elsif io_write='1' and io_addr(8)='1' then -- write IO2 => enable
                    mode_bits(0) <= '0';
                end if;
                exrom_n   <= mode_bits(0);
                serve_rom <= '1';

            -- SIMPLE ROM BANKERS
            when c_ocean_8K =>
                -- variant 0: always enabled
                -- variant 1: can be disabled by setting bit 7 to 1 (domark)
                -- variant 2: can be disabled by setting bit 6 to 1 (gmod2)
                if io_write='1' and io_addr(8)='0' then -- DE00 range
                    bank_bits(19 downto 14) <= io_wdata(5 downto 0); -- 64 banks of 8K
                    case variant is
                    when "000"|"100" =>
                        null;                        -- Always enabled
                    when "001"|"101" =>
                        mode_bits(0) <= io_wdata(7); -- DOMARK ROM disable
                    when "010"|"110" =>
                        mode_bits(1) <= io_wdata(7); -- gmod2 EEPROM write enable
                        mode_bits(0) <= io_wdata(6); -- gmod2 ROM disable
                        ee_sel       <= io_wdata(6);
                        ee_clk       <= io_wdata(5);
                        ee_wdata     <= io_wdata(4);
                    when others =>
                        null;
                    end case;
                end if;
                game_n    <= '1';
                exrom_n   <= mode_bits(0);
                serve_rom <= '1';
                cart_en   <= not mode_bits(0);
                rom_mode  <= "00"; -- 8K banks
                
            
            when c_ocean_16K =>
                if io_write='1' and io_addr(8)='0' then -- DE00 range
                    -- variant sets max number of banks, 000 = 4, 001 = 8, 011 = 16, 111 = 32
                    bank_bits(18 downto 14) <= io_wdata(4 downto 0) and (variant & "11"); -- max 32 banks of 16K
                end if;
                game_n    <= '0';
                exrom_n   <= '0';
                serve_rom <= '1';
                rom_mode  <= "01"; -- 16K banks

            when c_system3 => -- 16K, only 8K used?
                if (io_write='1' or io_read='1') and io_addr(8)='0' then -- DE00 range
                    bank_bits(19 downto 14) <= io_addr(5 downto 0); -- max 64 banks of 8K
                    -- turn on
                    mode_bits(0) <= '0';
                -- elsif io_read='1' and io_addr(8)='0' then
                --     -- turn off
                --     mode_bits(0) <= '1';
                end if;
                game_n    <= '1';
                exrom_n   <= mode_bits(0);
                serve_rom <= '1';
                rom_mode  <= "00"; -- 8K banks 

            when c_supergames =>
                if io_write='1' and io_addr(8)='1' and mode_bits(1) = '0' then -- DF00-DFFF
                    bank_bits(15 downto 14) <= io_wdata(1 downto 0); -- 4 banks of 16K
                    mode_bits(1 downto 0) <= io_wdata(3 downto 2);                
                end if;
                if mode_bits(1 downto 0) = "11" then -- Mostly to visualize
                    cart_en <= '0';
                end if;
                game_n    <= mode_bits(0);
                exrom_n   <= mode_bits(0); -- hmm?!
                serve_rom <= '1';
                rom_mode  <= "01"; -- 16K banks

            when c_blackbox_v8 =>
                if io_write='1' and io_addr(8)='1' then -- write to DFxx
                    bank_bits(15 downto 14) <= io_wdata(3 downto 2); -- 4 banks of 16K
                    mode_bits(1 downto 0) <= io_wdata(1 downto 0);
                end if;
                game_n    <= mode_bits(1);
                exrom_n   <= mode_bits(0);
                serve_rom <= '1';
                rom_mode  <= "01"; -- 16K banks

            when c_blackbox_v9 =>
                if io_write='1' and io_addr(8)='0' then -- write to DExx
                    bank_bits(14) <= not io_addr(7); -- 2 banks of 16K
                    mode_bits(1) <= io_addr(0);
                    mode_bits(0) <= not io_addr(6);
                end if;
                if io_read='1' and io_addr(8)='0' then
                    bank_bits(14) <= io_addr(7); -- 2 banks of 16K
                    mode_bits(1) <= io_addr(0);
                    mode_bits(0) <= not io_addr(6);
                end if;
                if reset_in='1' or cart_force = '1' then
                    bank_bits(14) <= '1'; -- start in last bank
                end if;
                if phi2='1' then
                    game_n    <= mode_bits(1);
                    exrom_n   <= not mode_bits(0);
                end if;
                serve_rom <= '1';
                serve_io1 <= '1';
                rom_mode  <= "01"; -- 16K banks

            when c_zaxxon =>
                -- a read from 8000-8FFF selects bank 0, a read from 9000-9FFF selects bank 1.
                if slot_req.sample_io = '1' and slot_addr(15 downto 13) = "100" and slot_rwn = '1' then
                    bank_bits(14) <= slot_addr(12);
                end if;
                game_n    <= '0';
                exrom_n   <= '0';
                serve_rom <= '1';
                rom_mode  <= "01"; -- 16K banks

            -- (SIMPLE) BANKERS WITH RAM
            when c_pagefox => -- 16K mode on/off, 4 banks
                if io_write='1' and io_addr(8 downto 7) = "01"  then -- DE80-DEFF
                    mode_bits <= io_wdata(4 downto 2); -- if mode_bits are 10X then map ram
                    bank_bits(15 downto 14) <= io_wdata(2 downto 1);
                    ram_bank(14) <= io_wdata(1);
                end if;
                ram_bank(13) <= slot_addr(13); -- :-) 
                game_n    <= mode_bits(2);
                exrom_n   <= mode_bits(2);
                serve_rom <= '1';
                rom_mode  <= "01"; -- 16K banks

            when c_easy_flash =>
                if io_write='1' and io_addr(8)='0' and cart_en='1' then -- DExx
                    ef_write <= '0';
                    v_addr4 := io_addr(3 downto 0);
                    case v_addr4 is
                    when X"0" =>
                        bank_bits(19 downto 14) <= io_wdata(5 downto 0); -- max 64 banks of 16K
                    when X"2" =>
                        mode_bits <= io_wdata(2 downto 0); -- LED not implemented
                    when X"9" =>
                        if io_wdata = X"65" then
                            ef_write <= '1';
                        end if;
                    when others =>
                        null;
                    end case;
                end if;
                game_n    <= not (mode_bits(0) or not mode_bits(2));
                exrom_n   <= not mode_bits(1);
                serve_rom <= '1';
                serve_io1 <= '0'; -- write registers only, no reads
                serve_io2 <= '1'; -- RAM
                rom_mode  <= "01"; -- 16K banks

            -- COMMON FREEZERS
            when c_fc3 =>
                if io_write='1' and io_addr(8 downto 0) = "111111111" and cart_en='1' then -- DFFF
                    bank_bits(15 downto 14) <= io_wdata(1 downto 0);
                    if variant(0)='1' then -- 256K version
                        bank_bits(17 downto 16) <= io_wdata(3 downto 2);
                    end if;
                    mode_bits <= '0' & io_wdata(4) & io_wdata(5);
                    unfreeze  <= '1';
                    cart_en   <= not io_wdata(7);
                    hold_nmi  <= not io_wdata(6);
                end if;
                if freeze_act='1' then
                    game_n  <= '0';
                    exrom_n <= '1';
                else
                    game_n  <= mode_bits(0);
                    exrom_n <= mode_bits(1);
                end if;
                if mode_bits(1 downto 0)="10" then
                    serve_vic <= '1';
                end if;
                serve_rom <= '1';
                serve_io1 <= '1';
                serve_io2 <= '1';
                nmi_n     <= not(freeze_trig or freeze_act or hold_nmi);
                rom_mode  <= "01"; -- 16K banks
                freezer_ena <= '1';
                                    
            when c_action =>
                -- variant bit 0: Retro Extension (0 = All of DExx / 1 = Only DE00/01, and REU compatible mapping / extra RAM)
                -- variant bit 1: Nordic extension (1 = special mode "110" that selects ultimax in A000-BFFF range)
                if io_write='1' and io_addr(8) = '0' and cart_en='1' and (io_addr(8 downto 1) = X"00" or variant(0)='0') then
                    if io_addr(0)='0' or variant(0)='0' then
                        bank_bits(16 downto 14) <= io_wdata(7) & io_wdata(4 downto 3);
                        mode_bits <= io_wdata(5) & io_wdata(1 downto 0);
                        unfreeze  <= io_wdata(6);
                        cart_en   <= not io_wdata(2);
                    elsif io_addr(0)='1' and variant(0)='1' then -- extended register for Retro Replay
                        if io_wdata(6)='1' then
                            do_io2 <= '0';
                        end if;
                        if io_wdata(1)='1' then
                            allow_bank <= '1';
                        end if;
                    end if;
                end if;
                if allow_bank = '1' then
                    ram_bank <= bank_bits(16 downto 14);
                else
                    ram_bank <= "000";
                end if;
                if freeze_act='1' then
                    game_n    <= '0';
                    exrom_n   <= '1';
                    serve_rom <= '1';
                else
                    serve_io1 <= c_serve_io_rr(to_integer(unsigned(mode_bits)));
                    serve_io2 <= c_serve_io_rr(to_integer(unsigned(mode_bits))) and do_io2;
                    serve_rom <= c_serve_rom_rr(to_integer(unsigned(mode_bits)));
                    if mode_bits(2 downto 0)="110" and variant(1)='1' then
                        game_n    <= '0';
                        -- Switch to Ultimax mode for writes to address A000-BFFF (disable C64 RAM write)
                        exrom_n   <= slot_addr(15) and not slot_addr(14) and slot_addr(13) and not slot_rwn;
                    else
                        game_n    <= not mode_bits(0);
                        exrom_n   <= mode_bits(1);
                    end if;
                end if;
                irq_n     <= not(freeze_trig or freeze_act);
                nmi_n     <= not(freeze_trig or freeze_act);
                rom_mode  <= "00"; -- 8K banks
                freezer_ena <= '1';
                
            when c_ss5 =>
                if io_write='1' and io_addr(8) = '0' and cart_en='1' then -- DE00-DEFF
                    bank_bits(15 downto 14) <= io_wdata(4) & io_wdata(2); -- 4 banks of 16K
                    if variant(0)='1' then -- 128K version
                        bank_bits(16) <= io_wdata(5);
                    end if;
                    mode_bits <= io_wdata(3) & io_wdata(1) & io_wdata(0);
                    unfreeze  <= not io_wdata(0);
                    cart_en   <= not io_wdata(3);
                end if;
                game_n    <= mode_bits(0);
                exrom_n   <= not mode_bits(1);
                serve_io1 <= cart_en;
                serve_io2 <= '0';
                serve_rom <= cart_en;
                irq_n     <= not(freeze_trig or freeze_act);
                nmi_n     <= not(freeze_trig or freeze_act);
                rom_mode  <= "01"; -- 16K banks
                freezer_ena <= '1';

            when c_kcs =>
                -- mode_bit(0) -> ULTIMAX
                -- mode_bit(1) -> 16K Mode
                -- io1 read
                if io_read='1' and io_addr(8) = '0' then -- DE00-DEFF
                    mode_bits(0) <= '1';            -- When read and addr bit 1=0 : 8k GAME mode            
                    mode_bits(1) <= io_addr(1);     -- When read and addr bit 1=1 : Cartridge disabled mode 
                    mode_bits(2) <= '0';
                end if;

                -- io1 write
                if io_write='1' and io_addr(8 downto 7) = "01" then -- DE80-DEFF
                    mode_bits <= "000"; -- 16K mode
                end if;
                if io_write='1' and io_addr(8 downto 7) = "00" then -- DE00-DE7F
                    -- if in 16K 000 / UmaxS 110 / Off2 111
                    if mode_bits = "000" then -- 16K
                        mode_bits <= "110";
                    elsif mode_bits = "010" or mode_bits = "111" then -- Freeze of Off2
                        mode_bits <= "000";          -- When addr bit 1=0 : 16k GAME mode
                        mode_bits(0) <= io_addr(1);  -- When addr bit 1=1 : 8k GAME mode 
                    end if;                    
                end if;
                -- io2 read
                if io_read='1' and io_addr(8 downto 7) = "11" then -- DF80-DFFF
                   unfreeze     <= '1';    -- When read : release freeze
                end if;
                -- on freeze
                if freeze_act='1' then
                    mode_bits <= "010";
                end if;

                game_n    <= mode_bits(0);
                exrom_n   <= mode_bits(1);
                serve_io1 <= '1';
                serve_io2 <= '1';
                serve_rom <= '1';
                serve_vic <= mode_bits(1);
                nmi_n     <= not(freeze_trig or freeze_act);
                freezer_ena <= '1';

            when c_fc =>
                -- io1 access
                if (io_read='1' or io_write='1') and io_addr(8) = '0' then -- DE00-DEFF
                    mode_bits(0) <= '1';
                    unfreeze  <= '1';
                end if;
                -- io2 access
                if (io_read='1' or io_write='1') and io_addr(8) = '1' then -- DF00-DFFF
                    mode_bits(0) <= '0';
                    unfreeze  <= '1';
                end if;
                -- Freezer runs in Ultimax mode
                if freeze_act='1' then
                    game_n       <= '0';   -- ULTIMAX mode
                    exrom_n      <= '1';
                else
                    game_n       <= mode_bits(0); -- 16K mode or off
                    exrom_n      <= mode_bits(0);
                end if;
                serve_io1 <= '1';
                serve_io2 <= '1';
                serve_rom <= '1';
                nmi_n     <= not(freeze_trig or freeze_act);
                freezer_ena <= '1';

            -- EXOTICS
            when c_georam =>
                if io_write='1' and io_addr(8 downto 7) = "11" then
                    if io_addr(0) = '0' then
                        georam_bank(5 downto 0) <= io_wdata(5 downto 0) and georam_mask(5 downto 0);
                        georam_bank(15 downto 14) <= io_wdata(7 downto 6) and georam_mask(15 downto 14);
                    else
                        georam_bank(13 downto 6) <= io_wdata(7 downto 0) and georam_mask(13 downto 6);
                    end if; 
                end if;
                serve_io1 <= '1';

            when others =>
                null;
            end case;

            if cart_kill='1' then
                cart_en  <= '0';
                hold_nmi <= '0';
                unfreeze <= '1';
            end if;
        end if;
    end process;

    CART_LEDn <= not cart_en;

    -- determine ROM address
    process(slot_addr, bank_bits, rom_mode)  -- Rom mode 00 = 8K banks, 01 = 16K banks, 11 = 32K banks
    begin
        rom_addr <= g_rom_base;
        rom_addr(12 downto 0) <= slot_addr(12 downto 0);
        rom_addr(19 downto 13) <= bank_bits;
        if rom_mode(13)='1' then
            rom_addr(13) <= slot_addr(13);
        end if;
        if rom_mode(14)='1' then
            rom_addr(14) <= slot_addr(14);
        end if;
    end process;

    -- Determine if RAM is mapped, and its address (max 64K)
    process(cart_logic_d, variant, mode_bits, ram_bank, slot_addr, do_io2, allow_bank, ef_write)
    begin
        -- Default
        ram_addr <= g_ram_base;
        ram_addr(15 downto 0) <= ram_bank & slot_addr(12 downto 0);
        allow_write <= '0';
        addr_map <= ROM;
        
        case cart_logic_d is
        when c_action =>
            if mode_bits(2)='1' then
                if slot_addr(13)='0' then -- True for 8000-9FFF, as well as IO1/IO2.
                    addr_map <= RAM;
                end if;
                if slot_addr(15 downto 13)="100" then -- 8000-9FFF
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DE" and slot_addr(7 downto 1)/="0000000" and variant(0)='1' then
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DF" and do_io2='1' then
                    allow_write <= '1';
                end if;
                if mode_bits(1 downto 0)="10" and variant(1)='1' then
                    if slot_addr(15 downto 13)="100" then
                        addr_map <= ROM;
                        allow_write <= '0';
                    elsif slot_addr(15 downto 13)="101" then
                        addr_map <= RAM;
                        allow_write <= '1';
                    end if;
                end if;
            end if;

        when c_easy_flash =>
            -- Little RAM
            if slot_addr(15 downto 8)=X"DF" then
                addr_map <= RAM;
                allow_write <= '1';
            elsif ef_write='1' and mode_bits="101" and (slot_addr(15 downto 13)="111" or slot_addr(15 downto 13)="100") then -- Ultimax mode, 8000-9FFF and
                allow_write <= '1';
            end if; 

        when c_ss5 =>
            if mode_bits(1 downto 0)="00" then
                if slot_addr(15 downto 13)="100" then
                    addr_map <= RAM;
                    allow_write <= '1';
                end if;
            end if;

        when c_kcs =>
            -- io2 ram access
            if slot_addr(15 downto 8) = X"DF" then
                ram_addr(7) <= '0';
                addr_map <= RAM;
                allow_write <= '1';
            end if;

        when c_georam =>
            if slot_addr(15 downto 8)=X"DE" then
                allow_write <= '1';
                addr_map <= GEO;
            end if;

        when c_128 =>
            if slot_addr(15 downto 8)=X"DF" and slot_addr(7)='1' and variant(2)='1' then
                allow_write <= '1';
                addr_map <= RAM;
            end if;
            
        when c_pagefox =>
            if mode_bits(1 downto 0)="10" then
                addr_map <= RAM;
        	if slot_addr(15 downto 14)="10" then
            	    allow_write <= '1';
		end if;
            end if;

        when others =>
            null;
        end case;
    end process;
    
    -- Calculate the final memory address
    process(addr_map, rom_addr, ram_addr, kernal_area, georam_bank, slot_addr) 
    begin
        case addr_map is
        when RAM =>
            mem_addr_i <= ram_addr;
        when GEO =>
            mem_addr_i <= g_georam_base(27 downto 24) & georam_bank & slot_addr(7 downto 0);
        when others =>        
            mem_addr_i <= rom_addr;
        end case;                

        if kernal_area='1' then -- This bit-order reduces the number of multiplexers
            mem_addr_i <= g_kernal_base(27 downto 15) & slot_addr(1 downto 0) & slot_addr(12 downto 2) & "00";
        end if;
    end process;

    mem_addr_c <= mem_addr_i when rising_edge(clock) and mem_req='0';
    mem_addr <= unsigned(mem_addr_c(mem_addr'range)) when g_register_addr else
                unsigned(mem_addr_i(mem_addr'range));

--    slot_resp.data(7) <= bank_bits(16);
--    slot_resp.data(6) <= '1';
--    slot_resp.data(5) <= '0';
--    slot_resp.data(4) <= bank_bits(15);
--    slot_resp.data(3) <= bank_bits(14);
--    slot_resp.data(2) <= '0'; -- freeze button pressed
--    slot_resp.data(1) <= allow_bank;
--    slot_resp.data(0) <= '0';
--    
--    slot_resp.reg_output <= '1' when (slot_addr(8 downto 1)="00000000") and (cart_logic_d = c_action) and (variant(0)='1') else '0';

    process(bank_bits, mode_bits, allow_bank, cart_logic_d, variant, slot_addr, ee_rdata)
    begin
        slot_resp <= c_slot_resp_init;
        
        case cart_logic_d is
        when c_action =>
            slot_resp.data(7) <= bank_bits(16);
            slot_resp.data(6) <= '1';
            slot_resp.data(5) <= '0';
            slot_resp.data(4) <= bank_bits(15);
            slot_resp.data(3) <= bank_bits(14);
            slot_resp.data(2) <= '0'; -- freeze button pressed
            slot_resp.data(1) <= allow_bank;
            slot_resp.data(0) <= '0';
            if slot_addr(8 downto 1) = X"00" and variant(1)='1' then
                slot_resp.reg_output <= '1';
            end if;
    
        when c_ocean_8K =>
            if g_eeprom then
                slot_resp.data(7) <= ee_rdata;
                if slot_addr(8) = '0' and variant(1 downto 0) = "10" then -- gmod2 variant, reading from DExx
                    slot_resp.reg_output <= '1';
                end if;
            end if;

        when others =>
            null;        
        end case;
    end process;

    r_ee: if g_eeprom generate
        i_ee: entity work.microwire_eeprom
        port map(
            clock    => clock,
            reset    => reset,
            io_req   => io_req_eeprom,
            io_resp  => io_resp_eeprom,
            sel_in   => ee_sel,
            clk_in   => ee_clk,
            data_in  => ee_wdata,
            data_out => ee_rdata
        );
    end generate;

    r_no_ee: if not g_eeprom generate
        i_ee_dummy: entity work.io_dummy
        port map(
            clock   => clock,
            io_req  => io_req_eeprom,
            io_resp => io_resp_eeprom
        );
    end generate;

end architecture;
