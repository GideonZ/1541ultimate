-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : fall_through_add_on
-------------------------------------------------------------------------------
-- Description: fall_through_add_on, one position deep fifo-add-on
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity fall_through_add_on is
    
    generic (
        g_data_width : natural := 32);
    port (
        clock      : in  std_logic;
        reset      : in  std_logic;
        -- fifo side
        rd_dout    : in  std_logic_vector(g_data_width - 1 downto 0);
        rd_empty   : in  std_logic;
        rd_en      : out std_logic;
        -- consumer side
        data_out   : out std_logic_vector(g_data_width - 1 downto 0);
        data_valid : out std_logic;
        data_next  : in  std_logic);

end fall_through_add_on;

architecture structural of fall_through_add_on is
    type t_state is (empty, full);
    signal state : t_state;
    
begin  -- structural

    fsm : process (clock)
    begin  -- process fsm
        if clock'event and clock = '1' then  -- rising clock edge
            case state is
                when empty =>
                    if rd_empty = '0' then
                        state <= full;
                    end if;
                when full =>
                    if data_next = '1' and rd_empty = '1' then
                        state <= empty;
                    end if;
                when others => null;
            end case;
            if reset = '1' then              -- synchronous reset (active high)
                state <= empty;
            end if;
        end if;
    end process fsm;

    data_valid <= '1' when state = full else 
                  '0';

    rd_en <= '1' when ((state = empty and rd_empty = '0') or
                       (state = full and rd_empty = '0' and data_next = '1')) else
             '0';

    data_out <= rd_dout;

end structural;
