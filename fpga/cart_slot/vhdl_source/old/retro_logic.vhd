library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity retro_logic is
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

    reg_output      : out std_logic;
    reg_rdata       : out std_logic_vector(7 downto 0);
    
    slot_addr       : in  std_logic_vector(15 downto 0);
    mem_addr        : out std_logic_vector(25 downto 0);   
    
    -- debug
    cart_mode       : out std_logic_vector(7 downto 0);

    irq_n           : out std_logic;
    nmi_n           : out std_logic;
    exrom_n         : out std_logic;
    game_n          : out std_logic;

    CART_LEDn       : out std_logic );

end retro_logic;    

architecture gideon of retro_logic is
    signal reset_in    : std_logic;
    signal cart_ctrl   : std_logic_vector(7 downto 0);
    signal freeze_act_d : std_logic;

    signal mode       : std_logic_vector(2 downto 0);
    signal cart_en    : std_logic;
    signal do_io2     : std_logic;
    signal allow_bank : std_logic;
    
    constant c_serve_rom : std_logic_vector(0 to 7) := "11011111";
    constant c_serve_io1 : std_logic_vector(0 to 7) := "10101111";
begin
    unfreeze     <= cart_ctrl(6);
    serve_enable <= cart_en;
    
    process(clock)
    begin
        if rising_edge(clock) then
            reset_in     <= reset or not RSTn_in;
            freeze_act_d <= freeze_act;
            
            -- control register
            if reset_in='1' then -- either reset or freeze
                cart_ctrl <= (others => '0');
                do_io2 <= '1';
                allow_bank <= '0';
            elsif freeze_act='1' and freeze_act_d='0' then
                cart_ctrl <= (others => '0');
                do_io2 <= '1';
                allow_bank <= '0';
            elsif io_write='1' and io_addr(8 downto 1) = X"00" and cart_en='1' then -- IO1
                if io_addr(0)='0' then
                    cart_ctrl <= io_data;
                else
                    do_io2     <= not io_data(6);
                    allow_bank <= io_data(1);
                end if;
            end if;

            -- Generate the cartridge mode
            -- determine whether to serve io requests
            if freeze_act='1' then
                game_n    <= '0';
                exrom_n   <= '1';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
            else
                game_n    <= not mode(0);
                exrom_n   <= mode(1);
                serve_io1 <= c_serve_io1(conv_integer(mode));
                serve_io2 <= c_serve_io1(conv_integer(mode)) and do_io2;
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
    process(slot_addr, mode, cart_ctrl, do_io2)
    begin
        allow_write <= '0';
        if mode(2)='1' then
            if slot_addr(13)='0' then
                if allow_bank='1' then
                    mem_addr <= ram_base(25 downto 15) & cart_ctrl(4 downto 3) & slot_addr(12 downto 0);
                else
                    mem_addr <= ram_base(25 downto 15) & "00" & slot_addr(12 downto 0);
                end if;
            else
                mem_addr <= rom_base(25 downto 16) & cart_ctrl(7) & cart_ctrl(4 downto 3) & slot_addr(12 downto 0);
            end if;
            if slot_addr(15 downto 13)="100" and cart_ctrl(1 downto 0)="11" then
                allow_write <= '1';
            end if;
            if slot_addr(15 downto 8)=X"DE" and slot_addr(7 downto 1)/="0000000" then
                allow_write <= '1';
            end if;
            if slot_addr(15 downto 8)=X"DF" and do_io2='1' then
                allow_write <= '1';
            end if;
        else
            mem_addr <= rom_base(25 downto 16) & cart_ctrl(7) & cart_ctrl(4 downto 3) & slot_addr(12 downto 0);
        end if;
    end process;

    cart_mode <= cart_ctrl;
    serve_vic <= '0';

    reg_rdata(7) <= cart_ctrl(7);
    reg_rdata(6) <= '1';
    reg_rdata(5) <= '0';
    reg_rdata(4) <= cart_ctrl(4);
    reg_rdata(3) <= cart_ctrl(3);
    reg_rdata(2) <= '0'; -- freeze button pressed
    reg_rdata(1) <= allow_bank; -- '1'; -- allow bank bit stuck at '1' for 1541U
    reg_rdata(0) <= '0';
    
    reg_output <= '1' when slot_addr(15 downto 1)="110111100000000" else '0';

end gideon;
