library ieee;
use ieee.std_logic_1164.all;

entity proc_interrupt is
port (
    clock       : in  std_logic;
    clock_en    : in  std_logic;
    clock_en_f  : in  std_logic;
    
    ready       : in  std_logic;
    reset       : in  std_logic;
    
    irq_n       : in  std_logic;
    nmi_n       : in  std_logic;
    
    i_flag      : in  std_logic;

    irq_done    : in  std_logic;
    interrupt   : out std_logic;
    vect_sel    : out std_logic_vector(2 downto 1) );

end proc_interrupt;

architecture gideon of proc_interrupt is
    signal irq_c    : std_logic := '1';
    signal irq_cc   : std_logic := '1';
    signal irq_d1   : std_logic := '0';
    signal irq_d2   : std_logic := '0';
    signal nmi_c    : std_logic := '0';
    signal nmi_d1   : std_logic := '0';
    signal nmi_d2   : std_logic := '0';
    signal nmi_act  : std_logic := '0';
    signal ready_d  : std_logic := '1';
    signal vect_h   : std_logic_vector(2 downto 1) := "10";
    type state_t is (idle, do_nmi); --, do_nmi, wait_vector);
    signal state : state_t;
    signal resetting : std_logic := '1';

    --         21
    -- NMI   1 01-
    -- RESET 1 10-
    -- IRQ   1 11-
    function calc_vec_addr(rst, irq, nmi : std_logic) return std_logic_vector is
        variable v : std_logic_vector(2 downto 1);
    begin
        if rst='1' then
            v := "10";
        elsif nmi='1' then
            v := "01";
        else
            v := "11";
        end if;
        return v;
    end function;

begin
    vect_sel <= vect_h;
    
    interrupt  <= ((irq_d1 and not i_flag) or nmi_act);
    
    process(clock)
    begin
        if rising_edge(clock) then
            nmi_c   <= not nmi_n;

            if clock_en='1' then
--                if ready='1' then
                    irq_c   <= irq_n;
--                end if;
            end if;
                        
            if clock_en='1' then
                if ready='1' then
                    nmi_d1 <= nmi_c;
                    irq_d1  <= not (irq_c);
                end if;
            end if;
        end if;
    end process;
        
    process(clock)
    begin
        if rising_edge(clock) then
            if clock_en='1' then
--                ready_d <= ready;
                
                nmi_d2 <= nmi_d1;
                if nmi_d2 = '0' and nmi_d1 = '1' then -- edge
                    nmi_act <= '1';
                end if;

                vect_h <= calc_vec_addr(resetting, irq_d1, nmi_act);

                case state is
                when idle =>
                    if nmi_act = '1' then
                        state   <= do_nmi;
                    end if;
                
                when do_nmi =>
                    if irq_done='1' then
                        nmi_act <= '0'; -- ###
                        state <= idle;
                        resetting <= '0';
                    end if;

                when others =>
                    state <= idle;
                
                end case;
            end if;

            if reset='1' then
                state   <= do_nmi;
                nmi_act <= '0';
                resetting <= '1';
                vect_h <= "10";
            end if;
        end if;
    end process;

end gideon;
