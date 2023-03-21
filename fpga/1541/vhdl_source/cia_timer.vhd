library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.cia_pkg.all;

entity cia_timer is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    prescale_en : in  std_logic;
    
    timer_ctrl  : in  timer_t;
    timer_set_t : in  std_logic;
    timer_in    : in  std_logic_vector(15 downto 0);
    
    cnt         : in  std_logic;
    othr_tmr_ev : in  std_logic;
    
    timer_out   : out std_logic;
    timer_event : out std_logic;
    timer_stop  : out std_logic;
    timer_count : out std_logic_vector(15 downto 0) );
end cia_timer;

architecture Gideon of cia_timer is
    signal mux_out              : unsigned(15 downto 0);
    signal count_next           : unsigned(15 downto 0);
    signal count_i              : unsigned(15 downto 0);
    signal timer_out_t          : std_logic := '0';
    signal cnt_d, cnt_c         : std_logic := '0';
    signal cnt_en               : std_logic := '0';
    signal cnt_en_d             : std_logic := '0';
    signal force_load           : std_logic := '0';
    signal event_i              : std_logic;
    signal event_d              : std_logic := '0';
    signal event_edge           : std_logic;
    signal load, carry          : std_logic;
    signal count_is_zero        : std_logic;
    signal run_mode_d           : std_logic := '0';
begin

    timer_out   <= event_i when timer_ctrl.outmode = '0'
            else   timer_out_t xor event_edge;

    timer_event <= event_i;
    timer_stop  <= event_i and (run_mode_d or timer_ctrl.runmode);

    process(timer_ctrl.inmode, timer_ctrl.run, timer_ctrl.load, cnt_c, cnt_d, othr_tmr_ev)
    begin
        cnt_en <= '0';
        case timer_ctrl.inmode is
            when "00" => cnt_en <= timer_ctrl.run and '1';
            when "01" => cnt_en <= timer_ctrl.run and cnt_c and not cnt_d;
            when "10" => cnt_en <= timer_ctrl.run and othr_tmr_ev;
            when "11" => cnt_en <= timer_ctrl.run and cnt_c and othr_tmr_ev;
            when others => null;
        end case;
    end process;
    
    timer_count        <= std_logic_vector(mux_out);
    mux_out            <= unsigned(timer_in) when load = '1' else count_i;
    count_next         <= mux_out - 1 when carry = '1' else mux_out;
    load               <= force_load or event_i;  
    carry              <= cnt_en_d and not load; 
    event_i            <= count_is_zero and cnt_en_d;
    event_edge         <= event_i and not event_d;
    
    count_is_zero      <= '1' when count_i = X"0000" else '0';
        
    process(clock)
    begin
        if rising_edge(clock) then
            if prescale_en='1' then
                run_mode_d <= timer_ctrl.runmode;
                force_load <= timer_ctrl.load;
                cnt_c <= cnt;
                cnt_d <= cnt_c;
                cnt_en_d <= cnt_en;
                event_d <= event_i;
                count_i <= count_next;
                
                if timer_set_t = '1' then
                    timer_out_t <= '1';
                elsif event_edge = '1' then
                    timer_out_t <= not timer_out_t; -- toggle
                end if;
            end if;

            if reset='1' then
                count_i     <= (others => '1');
                timer_out_t <= '0';
                force_load <= '0';
            end if;
        end if;
    end process;
end Gideon;
