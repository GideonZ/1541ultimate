library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity final3_logic is
generic (
    rom_base        : std_logic_vector(27 downto 0) := X"1010000" );
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
    
    -- debug
    cart_mode       : out std_logic_vector(7 downto 0);

    irq_n           : out std_logic;
    nmi_n           : out std_logic;
    exrom_n         : out std_logic;
    game_n          : out std_logic;

    CART_LEDn       : out std_logic );

end final3_logic;    

architecture gideon of final3_logic is
    signal cart_ctrl : std_logic_vector(7 downto 0);
    signal reset_in  : std_logic;
    signal cart_en   : std_logic;
begin
    serve_enable <= cart_en;

    process(clock)
    begin
        if rising_edge(clock) then
            reset_in <= not RSTn_in or reset;
            cart_en  <= not cart_ctrl(7) or freeze_act;
            unfreeze <= '0';

            -- flipflops
            if io_write='1' and io_addr = "111111111" then
                cart_ctrl <= io_data;
                unfreeze  <= freeze_act;
            end if;

            if cart_kill='1' then
                cart_ctrl(7) <= '1';
            end if;
            
            if cart_en='1' then
                exrom_n <= cart_ctrl(4);
                game_n  <= cart_ctrl(5) and not freeze_act;
            else
                game_n  <= '1';
                exrom_n <= '1';
            end if;

            -- reset
            if reset_in='1' then
                cart_ctrl <= X"00"; --(others => '0'); -- fix = X"20" => 8K rom only to test
            end if;
        end if;
    end process;
    
--    game_n   <= '0' when (cart_en='1') and (cnt_q(3)='0' or cart_ctrl(5)='0') else '1';
--    exrom_n  <= '0' when (cart_en='1') and (cart_ctrl(4)='0') else '1';

    -- open drains
    irq_n  <= '1';
    nmi_n  <= not(freeze_trig or freeze_act or not cart_ctrl(6));

    mem_addr     <= rom_base(25 downto 16) & cart_ctrl(1 downto 0) & slot_addr(13 downto 0);
    CART_LEDn    <= not cart_en;
    cart_mode    <= cart_ctrl;

    serve_vic    <= '1' when cart_ctrl(5 downto 4) = "01" else '0'; -- in Ultimax mode, serve VIC
    serve_rom    <= '1';
    serve_io1    <= '1';
    serve_io2    <= '1';
    allow_write  <= '0';
        
    -- cart_ctrl(3 downto 2) is unused
end gideon;
