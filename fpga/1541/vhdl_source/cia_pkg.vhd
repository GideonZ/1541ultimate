library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package cia_pkg is

type pio_t is
record
    pra     : std_logic_vector(7 downto 0);
    ddra    : std_logic_vector(7 downto 0);
    prb     : std_logic_vector(7 downto 0);
    ddrb    : std_logic_vector(7 downto 0);
end record;

constant pio_default : pio_t := (others => (others => '0'));

type timer_t is
record
    run     : std_logic;
    pbon    : std_logic;
    outmode : std_logic;
    runmode : std_logic;
    load    : std_logic;
    inmode  : std_logic_vector(1 downto 0);
end record;

constant timer_default : timer_t := (
    run     => '0',
    pbon    => '0',
    outmode => '0',
    runmode => '0',
    load    => '0',
    inmode  => "00" );
    
type time_t is
record
    pm          : std_logic;
    hr          : unsigned(4 downto 0);
    minh        : unsigned(2 downto 0);
    minl        : unsigned(3 downto 0);
    sech        : unsigned(2 downto 0);
    secl        : unsigned(3 downto 0);
    tenths      : unsigned(3 downto 0);
end record;

constant time_default  : time_t := ('1', "10001", "000", X"0", "000", X"0", X"0"); 
constant alarm_default : time_t := ('0', "00000", "000", X"0", "000", X"0", X"0"); 

type tod_t is
record
    alarm_set   : std_logic;
    freq_sel    : std_logic;
    tod         : time_t;
    alarm       : time_t;
end record;

constant tod_default : tod_t := (
    alarm_set   => '0',
    freq_sel    => '0',
    tod         => time_default,
    alarm       => alarm_default );
    
procedure do_tod_tick(signal t : inout time_t);

end;


package body cia_pkg is

procedure do_tod_tick(signal t : inout time_t) is
begin
    if t.tenths=X"9" then
        t.tenths <= X"0";
        if t.secl=X"9" then
            t.secl <= X"0";
            if t.sech="101" then
                t.sech <= "000";
                if t.minl=X"9" then
                    t.minl <= X"0";
                    if t.minh="101" then
                        t.minh <= "000";
                        if t.hr="01001" then
                            t.hr <= "10000";
                        elsif t.hr="01111" then
                            t.hr <= "00000";
                        elsif t.hr="11111" then
                            t.hr <= "10000";
                        elsif t.hr="10010" then
                            t.hr <= "00001";
                        elsif t.hr="10001" then
                            t.hr <= "10010";
                            t.pm <= not t.pm;
                        else
                            t.hr <= t.hr + 1;
                        end if;
                    else
                        t.minh <= t.minh + 1;
                    end if;
                else
                    t.minl <= t.minl + 1;
                end if;
            else
                t.sech <= t.sech + 1;
            end if;
        else
            t.secl <= t.secl + 1;
        end if;
    else
        t.tenths <= t.tenths + 1;
    end if;
end procedure do_tod_tick;

end;