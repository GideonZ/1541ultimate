library ieee;
use ieee.std_logic_1164.all;

entity freezer is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    RST_in          : in  std_logic;
    button_freeze   : in  std_logic;
    
    cpu_cycle_done  : in  std_logic;
    cpu_write       : in  std_logic;
    
    freezer_state   : out std_logic_vector(1 downto 0); -- debug

    unfreeze        : in  std_logic; -- could be software driven, or automatic, depending on cartridge
    freeze_trig     : out std_logic;
    freeze_act      : out std_logic );

end freezer;

architecture gideon of freezer is
    signal reset_in    : std_logic;
    signal wr_cnt      : integer range 0 to 3;
    signal do_freeze   : std_logic;
    signal do_freeze_d : std_logic;

    type t_state is (idle, triggered, enter_freeze, button);
    signal state : t_state;

begin
    freeze_trig <= '1' when (state = triggered) else '0';

    process(clock)
    begin
        if rising_edge(clock) then
            do_freeze   <= button_freeze;
            do_freeze_d <= do_freeze;
            reset_in <= reset or RST_in;

            if cpu_cycle_done='1' then
                if cpu_write='1' then
                    if wr_cnt/=3 then
                        wr_cnt <= wr_cnt + 1;
                    end if;
                else
                    wr_cnt <= 0;
                end if;
            end if;

            case state is
            when idle =>
                freeze_act <= '0';
                if do_freeze_d='0' and do_freeze='1' then -- push
                    state <= triggered;
                end if;

            when triggered =>
                if wr_cnt=3 then
                    state <= enter_freeze;
                    freeze_act <= '1';
                end if;
            
            when enter_freeze =>
                if unfreeze='1' then
                    freeze_act <= '0';
                    state <= button;
                end if;

            when button =>
                if do_freeze='0' then -- wait until button is not pressed anymore
                    state <= idle;
                end if;
                
            when others =>
                state <= idle;
            end case;
            
            if reset_in='1' then
                state <= idle;
                wr_cnt <= 0;
            end if; 
        end if;
    end process;
   
    with state select freezer_state <=
        "00" when idle,
        "01" when triggered,
        "10" when enter_freeze,
        "11" when button,
        "00" when others;

end gideon;
