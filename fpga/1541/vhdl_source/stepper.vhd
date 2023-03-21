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
    cur_track       : in  unsigned(6 downto 0);
    step_time       : in  unsigned(4 downto 0);
    enable          : in  std_logic := '1';
    busy            : out std_logic;    
    do_track_in     : out std_logic; -- for stand alone mode (pure 1581)
    do_track_out    : out std_logic;
    
    -- Index pulse
    motor_en        : in  std_logic;
    index_out       : out std_logic;

    -- emulated output as if stepping comes from VIA 
    step            : out std_logic_vector(1 downto 0) );

end entity;

architecture behavioral of stepper is
    constant c_max_track    : natural := 83;
    signal track    : unsigned(1 downto 0);
    signal timer    : unsigned(4 downto 0);
    
    signal index_cnt : unsigned(7 downto 0) := X"00";
begin        
    process(clock)
    begin
        if rising_edge(clock) then
            do_track_in <= '0';
            do_track_out <= '0';

            if timer /= 0 and tick_1KHz = '1' then
                timer <= timer - 1;
            end if;

            if enable = '0' then -- follow passively when disabled (i.e. when in 1541 mode)
                track <= unsigned(cur_track(1 downto 0));
            elsif timer = 0 then
                if (cur_track < goto_track) and (cur_track /= c_max_track) then
                    do_track_in <= '1';
                    track <= track + 1;
                    timer <= step_time;
                elsif cur_track > goto_track then
                    do_track_out <= '1';
                    track <= track - 1;
                    timer <= step_time;
                end if;
            end if;

            if reset = '1' then
                track <= "00";
                timer <= to_unsigned(1, 5);
            end if;
        end if;
    end process;
    step <= std_logic_vector(track);
    busy <= enable when (cur_track /= goto_track) else '0';    


    process(clock)
    begin
        if rising_edge(clock) then
            if tick_1kHz = '1' and motor_en = '1' then
                if index_cnt = 0 then
                    index_out <= '1';
                    index_cnt <= X"C7";
                else
                    index_out <= '0';
                    index_cnt <= index_cnt - 1;
                end if;
            end if;

            if reset = '1' then
                index_cnt <= X"C7";
                index_out <= '0';
            end if;
        end if;
    end process;

end architecture;
