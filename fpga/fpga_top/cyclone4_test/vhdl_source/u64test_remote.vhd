--------------------------------------------------------------------------------
-- Entity: u64test_remote
-- Date:2018-09-07  
-- Author: gideon     
--
-- Description: Remote registers on cartridge port for loopback testing of cart port
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity u64test_remote is
port  (
    arst    : in  std_logic;
    clock   : in  std_logic;

    BUTTON  : in  std_logic_vector(2 downto 0);

    VCC     : in  std_logic := '0';
    PHI2    : in  std_logic;
    DOTCLK  : in  std_logic;
    IO1n    : in  std_logic;
    IO2n    : in  std_logic;
    ROMLn   : in  std_logic;
    ROMHn   : in  std_logic;
    BA      : in  std_logic;
    RWn     : in  std_logic;
    
    GAMEn   : out   std_logic;
    EXROMn  : out   std_logic;
    DMAn    : out   std_logic;
    
    IRQn    : inout std_logic;
    NMIn    : inout std_logic;
    RESETn  : in   std_logic;
    
    D       : inout std_logic_vector(7 downto 0);
    A       : inout std_logic_vector(15 downto 0);
    
    LED_MOTORn  : out std_logic;
    LED_DISKn   : out std_logic;
    LED_CARTn   : out std_logic;
    LED_SDACTn  : out std_logic;

    IEC_SRQ_O   : out std_logic;
    IEC_RST_O   : out std_logic;
    IEC_CLK_O   : out std_logic;
    IEC_DATA_O  : out std_logic;
    IEC_ATN_O   : out std_logic;

    IEC_SRQ_I   : in  std_logic;
    IEC_RST_I   : in  std_logic;
    IEC_CLK_I   : in  std_logic;
    IEC_DATA_I  : in  std_logic;
    IEC_ATN_I   : in  std_logic;

    CAS_READ    : in  std_logic;
    CAS_WRITE   : in  std_logic;
    CAS_MOTOR   : in  std_logic;
    CAS_SENSE   : in  std_logic );

end entity;

architecture arch of u64test_remote is
    signal addr_en      : std_logic;
    signal ram_address  : unsigned(7 downto 0);
    signal ram_en       : std_logic;
    signal ram_we       : std_logic;
    signal ram_wdata    : std_logic_vector(7 downto 0);
    signal ram_rdata    : std_logic_vector(7 downto 0);
    signal iec_out      : std_logic_vector(4 downto 0);
    signal addr_out     : std_logic_vector(15 downto 0);
    signal cart_out     : std_logic_vector(7 downto 0);
    signal reg_out      : std_logic_vector(7 downto 0);
    signal led_out      : std_logic_vector(3 downto 0);

    signal cart_in      : std_logic_vector(7 downto 0) := X"00";
    signal iec_in       : std_logic_vector(7 downto 0) := X"00";
    signal cas_in       : std_logic_vector(7 downto 0) := X"00";

    signal phi2_d       : std_logic := '1';
    signal io2n_d       : std_logic := '1';
    signal rwn_d        : std_logic := '1';

    signal phi2_f       : std_logic;
    signal phi2_c       : std_logic;
begin
    phi2_f <= PHI2 when falling_edge(clock);
    phi2_c <= phi2_f when rising_edge(clock);
    phi2_d <= phi2_c when rising_edge(clock);    

    process(clock, arst)
        variable v_addr : std_logic_vector(2 downto 0);
    begin
        if arst = '1' then
            addr_en <= '0';

            iec_out <= (others => '0');
            addr_out <= (others => '0');
            cart_out <= (others => '0');
            led_out <= (others => '0');
        elsif rising_edge(clock) then
            if phi2_c = '0' and phi2_d = '1' and RWn = '0' and IO1n = '0' then
                v_addr := A(2 downto 0);
                case v_addr is
                when "000" =>
                    cart_out <= D(cart_out'range);
                when "010" =>
                    addr_out(15 downto 8) <= D;
                when "011" =>
                    addr_out(7 downto 0) <= D;
                when "100" =>
                    iec_out <= D(iec_out'range);
                when "110" =>
                    if D = X"B9" then
                        addr_en <= '1';
                    end if;
                when "111" =>
                    led_out <= D(led_out'range);
                    
                when others =>
                    null;
                end case;
            end if;
        end if;
    end process;

    i_ram: entity work.spram
    generic map (
        g_width_bits => 8,
        g_depth_bits => 8
    )
    port map (
        clock        => clock,
        address      => ram_address,
        rdata        => ram_rdata,
        wdata        => ram_wdata,
        en           => ram_en,
        we           => ram_we
    );
    
    ram_en      <= '1';

    process(clock)
    begin
        if rising_edge(clock) then
            ram_address <= unsigned(A(7 downto 0));
            io2n_d <= IO2n;
            rwn_d  <= RWn;
            ram_wdata <= D;
        end if;
    end process;
    ram_we <= '1' when phi2_d = '1' and phi2_c = '0' and io2n_d = '0' and rwn_d = '0' else '0';
    
    with A(2 downto 0) select reg_out <=
        cart_out       when "000",
        cart_in        when "001",
        A(15 downto 8) when "010",
        A(7 downto 0)  when "011", -- lower 3 bits will always read as "011"
        iec_in         when "100",
        cas_in         when "101",
        X"99"          when others;

    process(IO1n, IO2n, RWn, reg_out, ram_rdata)
    begin
        D <= (others => 'Z');
        if RWn = '1' then -- read cycle
            if IO1n = '0' then
                D <= reg_out;
            elsif IO2n = '0' then
                D <= ram_rdata;
            end if;
        end if;
    end process;         

    process(addr_en, BA, addr_out)
    begin
        A <= (others => 'Z');
        if addr_en = '1' and BA = '1' then
            A <= addr_out;
        end if;
    end process;

    -- IEC pins
    IEC_ATN_O   <= iec_out(0);
    IEC_CLK_O   <= iec_out(1);
    IEC_DATA_O  <= iec_out(2);
    IEC_SRQ_O   <= iec_out(3);
    IEC_RST_O   <= iec_out(4);

    iec_in <= "000" & IEC_RST_I & IEC_SRQ_I & IEC_DATA_I & IEC_CLK_I & IEC_ATN_I;

    -- CAS pins
    cas_in <= "0000" & CAS_SENSE & CAS_WRITE & CAS_READ & CAS_MOTOR;
    
    -- CART pins
    cart_in(0) <= IRQn;
    cart_in(1) <= NMIn;
    cart_in(2) <= ROMLn;
    cart_in(3) <= ROMHn;
    cart_in(4) <= DOTCLK;
    cart_in(5) <= RESETn;
    cart_in(6) <= BA;
    cart_in(7) <= VCC;

    IRQn       <= '0' when cart_out(0) = '1' or button(2) = '0' else 'Z';
    NMIn       <= '0' when cart_out(1) = '1' or button(1) = '0' else 'Z';
    GAMEn      <= '0' when cart_out(2) = '1' or button(0) = '0' else 'Z';
    EXROMn     <= '0' when cart_out(3) = '1' else 'Z';
    DMAn       <= '0' when cart_out(4) = '1' else 'Z';
    -- RESETn     <= '0' when cart_out(5) <= '1' else 'Z';

    LED_MOTORn <= '0' when led_out(0) = '1' else 'Z'; 
    LED_DISKn  <= '0' when led_out(1) = '1' else 'Z'; 
    LED_CARTn  <= '0' when led_out(2) = '1' else 'Z'; 
    LED_SDACTn <= '0' when led_out(3) = '1' else 'Z'; 

end architecture;

