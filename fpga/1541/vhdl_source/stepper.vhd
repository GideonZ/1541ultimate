library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity stepper is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- timing
    tick_1KHz       : in  std_logic;
    
    -- track interface from wd177x
    goto_track      : in  unsigned(6 downto 0);
    phys_track      : out unsigned(6 downto 0);
    step_time       : in  unsigned(4 downto 0);

    -- audio interface 
    do_track_out    : out std_logic := '0';
    do_track_in     : out std_logic := '0' );

end entity;

architecture behavioral of stepper is
    constant c_max_track    : natural := 82;
    signal track    : unsigned(6 downto 0);
    signal timer    : unsigned(4 downto 0);
begin        
    process(clock)
    begin
        if rising_edge(clock) then
            do_track_out <= '0';
            do_track_in  <= '0';

            if timer /= 0 and tick_1KHz = '1' then
                timer <= timer - 1;
            end if;

            if timer = 0 then
                if (track < goto_track) and (track /= c_max_track) then
                    do_track_in <= '1';
                    track <= track + 1;
                    timer <= step_time;
                elsif track > goto_track then
                    do_track_out <= '1';
                    track <= track - 1;
                    timer <= step_time;
                end if;
            end if;

            if reset = '1' then
                track <= to_unsigned(1, 7);
                timer <= to_unsigned(1, 5);
            end if;
        end if;
    end process;
    phys_track <= track;
    
end architecture;
