library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity action_logic is
generic (
    rom_base        : std_logic_vector(27 downto 0) := X"1040000";
    ram_base        : std_logic_vector(27 downto 0) := X"0052000" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    RSTn_in         : in  std_logic;

    freeze_trig     : in  std_logic; -- goes '1' when the button has been pressed and we're waiting to enter the freezer
    freeze_act      : in  std_logic; -- goes '1' when we need to switch in the cartridge for freeze mode
    unfreeze        : out std_logic; -- indicates the freeze logic to switch back to non-freeze mode.
    
    cart_kill       : in  std_logic;
    
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
    
    -- debug
    cart_mode       : out std_logic_vector(7 downto 0);

    irq_n           : out std_logic;
    nmi_n           : out std_logic;
    exrom_n         : out std_logic;
    game_n          : out std_logic;

    CART_LEDn       : out std_logic );

end action_logic;    

architecture gideon of action_logic is
    signal reset_in    : std_logic;
    signal cart_ctrl   : std_logic_vector(7 downto 0);
    signal freeze_act_d : std_logic;

    signal mode      : std_logic_vector(2 downto 0);
    signal cart_en   : std_logic;

    constant c_serve_rom : std_logic_vector(0 to 7) := "11011111";
    constant c_serve_io2 : std_logic_vector(0 to 7) := "10101111";
begin
    unfreeze     <= cart_ctrl(6);
    serve_enable <= cart_en;
    
    process(clock)
    begin
        if rising_edge(clock) then
            reset_in     <= reset or not RSTn_in;
            freeze_act_d <= freeze_act;
            
            -- control register
            if reset_in='1' or (freeze_act='1' and freeze_act_d='0') then -- either reset or freeze
                cart_ctrl <= (others => '0');
            elsif io_write='1' and io_addr(8 downto 1) = X"00" and cart_en='1' then -- IO1
                cart_ctrl <= io_data;
            end if;

            -- Generate the cartridge mode
            -- determine whether to serve io requests
            if freeze_act='1' then
                game_n    <= '0';
                exrom_n   <= '1';
                serve_io2 <= '0';
                serve_rom <= '1';
            else
                game_n    <= not mode(0);
                exrom_n   <= mode(1);
                serve_io2 <= c_serve_io2(conv_integer(mode));
                serve_rom <= c_serve_rom(conv_integer(mode));
            end if;

            if cart_kill='1' then
                cart_ctrl(2) <= '1';
            end if;
        end if;
    end process;
    
    mode      <= cart_ctrl(5) & cart_ctrl(1) & cart_ctrl(0);
    cart_en   <= not cart_ctrl(2);
    CART_LEDn <= cart_ctrl(2);

    irq_n     <= not (freeze_trig or freeze_act);
    nmi_n     <= not (freeze_trig or freeze_act);

    -- determine address
    process(slot_addr, mode, cart_ctrl)
    begin
        allow_write <= '0';
        if mode(2)='1' then
            if slot_addr(13)='0' then
                mem_addr <= ram_base(25 downto 13) & slot_addr(12 downto 0);
            else
                mem_addr <= rom_base(25 downto 15) & cart_ctrl(4 downto 3) & slot_addr(12 downto 0);
            end if;
            if slot_addr(15 downto 13)="100" or slot_addr(15 downto 8)=X"DF" then
                allow_write <= '1';
            end if;
        else
            mem_addr <= rom_base(25 downto 15) & cart_ctrl(4 downto 3) & slot_addr(12 downto 0);
        end if;
    end process;

    cart_mode <= cart_ctrl;
    serve_vic <= '0';
    serve_io1 <= '0';
end gideon;

--Freeze:
--Always use ROM address, and respond to ROML/ROMH, but not to IO2n
--
--Non-freeze:
--
--Mode 0: Always use ROM address, let ROMLn and IO2n control the output
--Mode 1: Always use ROM address, let ROMLn (ROMhn?) but not IO2n control the output
--Mode 2: Always use ROM address, and let only IO2n control the output
--Mode 3: Always use ROM address, and let ROMLn / ROMHn, but not IO2n control the output
--
--Mode 4: Always use RAM address, and let ROMLn and IO2n control the output
--Mode 5: Use A13 to select between ROM/RAM (0=RAM, 1=ROM), let ROMLn/ROMHn and IO2n control the output (or write)
--Mode 6: Always use RAM address, let IO2n and ROMLn control the output
--Mode 7: Use A13 to select between ROM/RAM (0=RAM, 1=ROM), let ROMLn/ROMHn and IO2n control the output (or write)
--
--$0000-$1FFF: ROM ---  8K ROM|ROM --- 16K ROM|--- ---  ----- |ROM --- UltiMax|--- ---  8K ROM|--- --- 16K ROM|--- ---  ----- |--- --- UltiMax|
--$2000-$3FFF: ROM ---  8K ROM|ROM --- 16K ROM|--- ---  ----- |ROM --- UltiMax|--- ---  8K ROM|--- --- 16K ROM|--- ---  ----- |--- --- UltiMax|
--$4000-$5FFF: ROM ---  8K ROM|ROM --- 16K ROM|--- ---  ----- |ROM --- UltiMax|--- ---  8K ROM|--- --- 16K ROM|--- ---  ----- |--- --- UltiMax|
--$6000-$7FFF: ROM ---  8K ROM|ROM --- 16K ROM|--- ---  ----- |ROM --- UltiMax|--- ---  8K ROM|--- --- 16K ROM|--- ---  ----- |--- --- UltiMax|
--$8000-$9FFF: ROM ---  8K ROM|--- --- 16K ROM|--- ---  ----- |ROM --- UltiMax|--- ram  8K ROM|--- ram 16K ROM|--- ram  ----- |--- ram UltiMax|
--$A000-$BFFF: --- ---  8K ROM|ROM --- 16K ROM|--- ---  ----- |--- --- UltiMax|--- ---  8K ROM|ROM --- 16K ROM|--- ---  ----- |ROM --- UltiMax|
--$C000-$DFFF: --- ---  8K ROM|--- --- 16K ROM|--- ---  ----- |--- --- UltiMax|--- ---  8K ROM|--- --- 16K ROM|--- ---  ----- |--- --- UltiMax|
--$E000-$FFFF: --- ---  8K ROM|--- --- 16K ROM|--- ---  ----- |ROM --- UltiMax|--- ---  8K ROM|--- --- 16K ROM|--- ---  ----- |ROM --- UltiMax|
--
--$DF00-$DFFF: ROM ---  8K ROM|--- --- 16K ROM|ROM ---  ----- |--- --- UltiMax|--- ram  8K ROM|--- ram 16K ROM|--- ram  ----- |--- ram UltiMax|