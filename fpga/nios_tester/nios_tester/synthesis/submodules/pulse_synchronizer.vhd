-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2014, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Pulse synchronizer block
-------------------------------------------------------------------------------
-- Description: The pulse synchronizer block synchronizes pulse in one
--              clock domain into a pulse in the other clock domain.
--
--              Please read Ran Ginosars paper "Fourteen ways to fool your
--              synchronizer" before considering modifications to this module!
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;

entity pulse_synchronizer is
    port (
        clock_in    : in  std_logic;
        pulse_in    : in  std_logic;
        
        clock_out   : in  std_logic;
        pulse_out   : out std_logic );

    ---------------------------------------------------------------------------
    -- Synthesis attributes to prevent balancing.
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_balancing                            : string;
    attribute register_balancing of pulse_synchronizer      : entity is "no";
    -- Altera attributes
    attribute dont_retime                                   : boolean;
    attribute dont_retime of pulse_synchronizer             : entity is true;
    ---------------------------------------------------------------------------

end pulse_synchronizer;

architecture rtl of pulse_synchronizer is

    signal in_toggle    : std_logic := '0';
    signal sync1        : std_logic := '0';
    signal sync2        : std_logic := '0';
    signal sync3        : std_logic := '0';

    ---------------------------------------------------------------------------
    -- Synthesis attributes to prevent duplication 
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_duplication             : string;
    attribute register_duplication of sync1    : signal is "no";
    attribute register_duplication of sync2    : signal is "no";

    -- Altera attributes
    attribute dont_replicate                   : boolean;
    attribute dont_replicate of sync1          : signal is true;
    attribute dont_replicate of sync2          : signal is true;
    ---------------------------------------------------------------------------

begin
    p_input_toggle : process(clock_in)
    begin
        if rising_edge(clock_in) then
            if pulse_in='1' then
                in_toggle <= not in_toggle;
            end if;
        end if;
    end process;
    
    p_synchronization : process(clock_out)
    begin
        if rising_edge(clock_out) then
            sync1 <= in_toggle;
            sync2 <= sync1;
            sync3 <= sync2;
            pulse_out <= sync2 xor sync3;
        end if;
    end process;

end rtl;
