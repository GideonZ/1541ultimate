-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2004, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Serial Receiver: 115200/8N1
-------------------------------------------------------------------------------
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-- Created    : Wed Apr 28, 2004
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rx is
--generic (clks_per_bit : integer := 434); -- 115k2 @ 50 MHz
port (
    divisor : in  std_logic_vector(9 downto 0);
    clk     : in  std_logic;
    reset   : in  std_logic;
    tick    : in  std_logic;
    rxd     : in  std_logic;
    
    rxchar  : out std_logic_vector(7 downto 0);
    rx_ack  : out std_logic );
end rx;

architecture gideon of rx is
    signal bitcnt : integer range 0 to 8;
    signal bitvec : std_logic_vector(8 downto 0);
    signal timer  : unsigned(9 downto 0);
    type state_t is (Idle, StartBit, Receiving);
    signal state  : state_t;
    signal rxd_c  : std_logic;
begin
    process(clk, reset)
    begin
        if clk'event and clk='1' then
            rxd_c  <= rxd;
            rx_ack <= '0';
            if tick = '1' then
                case state is
                when Idle =>
                    if rxd_c = '0' then
                        timer <= '0' & unsigned(divisor(9 downto 1)); --(clks_per_bit / 2) - 1;
                        state <= startbit;
                    end if;
                
                when StartBit =>
                    if rxd_c = '1' then
                        state <= Idle;
                    elsif timer = 0 then
                        timer  <= unsigned(divisor); --clks_per_bit - 1;
                        state  <= receiving;
                        bitcnt <= 8;
                    else
                        timer <= timer - 1;
                    end if;
                
                when Receiving =>
                    if timer=0 then
                        timer  <= unsigned(divisor); --clks_per_bit - 1;
                        bitvec <= rxd_c & bitvec(8 downto 1);
                        if bitcnt = 0 then
                            state  <= Idle;
                            rx_ack <= '1';
                        else
                            bitcnt <= bitcnt - 1;
                        end if;
                    else
                        timer <= timer - 1;
                    end if;
                end case;                
            end if;
        end if;
        if reset='1' then
            state  <= Idle;
            bitcnt <= 0;
            timer  <= (others => '0');
            bitvec <= (others => '0');
        end if;
    end process;
    
    rxchar <= bitvec(7 downto 0);
end gideon;
