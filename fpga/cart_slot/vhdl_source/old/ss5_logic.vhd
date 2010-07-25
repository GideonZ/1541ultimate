library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ss5_logic is
generic (
    ram_base        : std_logic_vector(27 downto 0) := X"0052000";
    rom_base        : std_logic_vector(27 downto 0) := X"1030000" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    RSTn_in         : in  std_logic;
    cart_kill       : in  std_logic := '0';

    freeze_trig     : in  std_logic;
    freeze_act      : in  std_logic;
    unfreeze        : out std_logic;
        
    io_write        : in  std_logic;
    io_addr         : in  std_logic_vector(8 downto 0);    
    io_data         : in  std_logic_vector(7 downto 0);

    serve_enable    : out std_logic; -- enables fetching bus address PHI2=1
    serve_vic       : out std_logic; -- enables doing so for PHI2=0
    serve_rom       : out std_logic; -- ROML or ROMH
    serve_io1       : out std_logic; -- IO1n
    serve_io2       : out std_logic; -- IO2n
    allow_write     : out std_logic;

    slot_addr       : in  std_logic_vector(15 downto 0);
    mem_addr        : out std_logic_vector(25 downto 0);   
    
    irq_n           : out std_logic;
    nmi_n           : out std_logic;
    exrom_n         : out std_logic;
    game_n          : out std_logic;

    CART_LEDn       : out std_logic );

end ss5_logic;    

architecture gideon of ss5_logic is
    signal cart_ctrl   : std_logic_vector(7 downto 0);
    signal reset_in    : std_logic;
    signal cart_en     : std_logic;
    signal freeze_act_d: std_logic;
    alias rom_enable_n : std_logic is cart_ctrl(3);
    alias ram_enable_n : std_logic is cart_ctrl(1);
    alias game_ctrl_n  : std_logic is cart_ctrl(0);
    alias exrom_ctrl   : std_logic is cart_ctrl(1);
    signal bank_reg    : std_logic_vector(1 downto 0);
    signal mode        : std_logic_vector(2 downto 0);
    constant c_serve_en : std_logic_vector(0 to 7) := "11111000";
    
-- Bit 0 controls the GAME line: 0 pulls it low, 1 leaves it high
-- Bit 1 enables Ram: 0= enabled, 1=disabled
--       this bit also controls the Exrom line: 0: Exrom=1, 1:Exrom=0 (inverted!)
-- Bit 2 banking address line 14
-- Bit 3 enables Rom: 0=enabled, 1=disabled. Caution: Disabling rom also
--       disables this register, so make sure that Ram is enabled in order to
--       release the Exrom line!
-- Bit 4 banking address line 15
-- Bit 5 banking address line 16

begin
    mode     <= cart_ctrl(3) & cart_ctrl(1) & cart_ctrl(0);
    bank_reg <= cart_ctrl(4) & cart_ctrl(2);
    exrom_n  <= not exrom_ctrl;
    game_n   <=     game_ctrl_n;

    unfreeze <= not game_ctrl_n; -- game goes low, unfreeze goes high

    process(clock)
    begin
        if rising_edge(clock) then
            reset_in <= not RSTn_in or reset;
            freeze_act_d <= freeze_act;
            
            -- flipflops
            if io_write='1' and io_addr(8) = '0' and rom_enable_n='0' then
                cart_ctrl <= io_data;
            end if;

            if cart_kill='1' then
                rom_enable_n <= '1';
                ram_enable_n <= '1';
                game_ctrl_n  <= '1';
                exrom_ctrl   <= '0';
            end if;
            
            -- reset
            if reset_in='1' or (freeze_act='1' and freeze_act_d='0') then
                cart_ctrl <= X"00";
            end if;
        end if;
    end process;
    
    -- open drains
    irq_n    <= not(freeze_trig or freeze_act);
    nmi_n    <= not(freeze_trig or freeze_act);
    
    -- determine address
    process(slot_addr, mode, cart_ctrl)
    begin
        allow_write <= '0';
        if mode(1 downto 0)="00" then
            if slot_addr(15 downto 13)="100" then
                allow_write <= '1';
                mem_addr <= ram_base(25 downto 13) & slot_addr(12 downto 0);
            else
                mem_addr <= rom_base(25 downto 16) & bank_reg & slot_addr(13 downto 0);
            end if;
        else
            mem_addr <= rom_base(25 downto 16) & bank_reg & slot_addr(13 downto 0);
        end if;
    end process;

    CART_LEDn    <= rom_enable_n;

    serve_vic    <= '0';
    serve_rom    <= not rom_enable_n;
    serve_io1    <= not rom_enable_n;
    serve_io2    <= not rom_enable_n;
        
    -- cart_ctrl(7 downto 6) is unused
    serve_enable <= c_serve_en(to_integer(unsigned(mode)));
end gideon;

-- %xxxx0x00: game=0, exrom=1. 8000=RAM E000=ROM IO=ROM  (ultimax)   RAM  ROM
-- %xxxx0x01: game=1, exrom=1. 8000=--- A000=--- IO=ROM             [RAM] ROM
-- %xxxx0x10: game=0, exrom=0. 8000=ROM A000=ROM IO=ROM                   ROM
-- %xxxx0x11: game=1, exrom=0. 8000=ROM A000=--- IO=ROM                   ROM
-- %xxxx1x00: game=0, exrom=1. 8000=RAM E000=--- IO=---  (ultimax)   RAM  
-- %xxxx1x01: game=1, exrom=1. 8000=--- A000=--- IO=---             [RAM]
-- %xxxx1x10: game=0, exrom=0. 8000=XXX A000=XXX IO=---
-- %xxxx1x11: game=1, exrom=0. 8000=XXX A000=--- IO=---

