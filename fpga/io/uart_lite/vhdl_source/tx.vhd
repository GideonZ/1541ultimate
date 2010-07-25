-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2004, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Serial Transmitter: 115200/8N1
-------------------------------------------------------------------------------
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-- Created    : Wed Apr 28, 2004
-------------------------------------------------------------------------------
-- Description: This module sends a character over a serial line
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity tx is
generic (clks_per_bit : integer := 434); -- 115k2 @ 50 MHz
port (
    clk     : in  std_logic;
    reset   : in  std_logic;
    
    dotx    : in  std_logic;
    txchar  : in  std_logic_vector(7 downto 0);
    cts     : in  std_logic := '1';
    txd     : out std_logic;
    done    : out std_logic );
end tx;

architecture gideon of tx is
    signal bitcnt : integer range 0 to 9;
    signal bitvec : std_logic_vector(8 downto 0);
    signal timer  : integer range 0 to clks_per_bit;
    type state_t is (Idle, Waiting, Transmitting);
    signal state  : state_t;
    signal cts_c  : std_logic := '1';
begin
    process(clk, reset)
    begin
        if rising_edge(clk) then
            cts_c <= cts;
            case state is
            when Idle =>
                if DoTx='1' then
                    if cts_c='1' then
                        state <= Transmitting;
                    else
                        state <= Waiting;
                    end if;
                    bitcnt <= 9;
                    bitvec <= not(txchar) & '1';
                    timer  <= clks_per_bit - 1;
                end if;
            when Waiting =>
                if cts_c='1' then
                    state <= Transmitting;
                end if;
            when Transmitting =>
                if timer=0 then
                    timer <= clks_per_bit - 1;
                    if bitcnt = 0 then
                        state <= Idle;
                    else
                        bitcnt <= bitcnt - 1;
                        bitvec <= '0' & bitvec(8 downto 1);
                    end if;
                else
                    timer <= timer - 1;
                end if;
            end case;                
        end if;
        if reset='1' then
            state  <= Idle;
            bitcnt <= 0;
            timer  <= 0;
            bitvec <= (others => '0');
        end if;
    end process;
    
    done <= '1' when state=Idle else '0';
    txd  <= not(bitvec(0));
end gideon;
