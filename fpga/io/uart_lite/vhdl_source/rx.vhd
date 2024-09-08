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
    divisor : in  std_logic_vector(10 downto 0);
    clk     : in  std_logic;
    reset   : in  std_logic;
    tick    : in  std_logic;
    rxd     : in  std_logic;
    
    timeout : out std_logic;
    rxchar  : out std_logic_vector(7 downto 0);
    rx_ack  : out std_logic );
end rx;

architecture gideon of rx is
    signal bitcnt : integer range 0 to 15;
    signal bitvec : std_logic_vector(8 downto 0);
    signal timer  : unsigned(10 downto 0);
    type state_t is (Idle, StartBit, Receiving);
    signal state  : state_t;
    signal rxd_s  : std_logic;
    signal rxd_c  : std_logic;
    signal rxd_d  : std_logic;
    signal trigger: std_logic;
begin
    process(clk, reset)
    begin
        if rising_edge(clk) then
            rxd_s  <= rxd;
            rxd_c  <= rxd_s;
            rxd_d  <= rxd_c;
            rx_ack <= '0';
            timeout <= '0';
            
            if tick = '1' then
                if timer = 0 then
                    timer  <= unsigned(divisor); --clks_per_bit - 1;
                    bitvec <= rxd_c & bitvec(8 downto 1);
                    if bitcnt = 0 then
                        bitcnt <= 8;
                    else
                        bitcnt <= bitcnt - 1;
                    end if;
                else
                    timer <= timer - 1;
                end if;

                case state is
                when Idle =>
                    if rxd_d = '1' and rxd_c = '0' then -- falling edge
                        timeout <= '0';
                        timer <= '0' & unsigned(divisor(10 downto 1)); --(clks_per_bit / 2) - 1;
                        state <= startbit;
                    elsif timer = 0 and bitcnt = 0 then
                        timeout <= trigger;
                        trigger <= '0';
                    end if;
                
                when StartBit =>
                    if rxd_c = '1' then
                        state <= Idle;
                    elsif timer = 0 then
                        state  <= receiving;
                        bitcnt <= 8;
                    end if;
                
                when Receiving =>
                    trigger<= '1';
                    if timer = 0 and bitcnt = 0 then
                        state  <= Idle;
                        rx_ack <= '1';
                    end if;
                end case;                
            end if;
        end if;
        if reset='1' then
            state  <= Idle;
            bitcnt <= 8;
            timer  <= (others => '0');
            bitvec <= (others => '0');
            rxd_c  <= '1';
            trigger<= '0';
        end if;
    end process;
    
    rxchar <= bitvec(7 downto 0);
end gideon;
