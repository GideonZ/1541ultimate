-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2008, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Level synchronizer block
-------------------------------------------------------------------------------
-- Description: The level synchronizer block synchronizes an asynchronous
--              input to the clock of the receiving module. Two flip-flops are
--              used to avoid metastability of the synchronized signal.
--
--              Please read Ran Ginosars paper "Fourteen ways to fool your
--              synchronizer" before considering modifications to this module!
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;

entity level_synchronizer is
    generic (
        g_reset_val : std_logic := '0'
    );
    port (
        -- Clock signal
        clock       : in  std_logic;
        -- Asynchronous input
        reset       : in  std_logic := '0';
        -- Asynchronous input
        input       : in  std_logic;
        -- Synchronized input
        input_c     : out std_logic
    );

    ---------------------------------------------------------------------------
    -- Synthesis attributes to prevent duplication and balancing.
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_duplication                          : string;
    attribute register_duplication of level_synchronizer    : entity is "no";
    attribute register_balancing                            : string;
    attribute register_balancing of level_synchronizer      : entity is "no";
    -- Altera attributes
    attribute dont_replicate                                : boolean;
    attribute dont_replicate of level_synchronizer          : entity is true;
    attribute dont_retime                                   : boolean;
    attribute dont_retime of level_synchronizer             : entity is true;
    ---------------------------------------------------------------------------

end level_synchronizer;

architecture rtl of level_synchronizer is

    signal sync1        : std_logic := g_reset_val;
    signal sync2        : std_logic := g_reset_val;

begin

    p_input_synchronization : process(clock)
    begin
        if rising_edge(clock) then

            sync1 <= input;
            sync2 <= sync1;

            if reset = '1' then
                sync1 <= g_reset_val;
                sync2 <= g_reset_val;
            end if;
        end if;
    end process;

    input_c <= sync2;

end rtl;
