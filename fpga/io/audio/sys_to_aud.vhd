--------------------------------------------------------------------------------
-- Entity: sys_to_aud
-- Date:2018-08-02  
-- Author: gideon     
--
-- Description: Audio sample data in sysclock domain to audio domain filter and synchronizer
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sys_to_aud is
generic (
    g_apply_filter  : boolean := true
);
port (
    sys_clock       : in  std_logic;
    sys_reset       : in  std_logic;
    sys_tick        : in  std_logic;
    sys_data        : in  signed(17 downto 0);
    audio_clock     : in  std_logic;
    audio_data      : out signed(17 downto 0)
);
end entity;

architecture arch of sys_to_aud is
    signal sys_filtered : signed(17 downto 0);
    signal audio_data_i : std_logic_vector(17 downto 0);
begin
    r_filt: if g_apply_filter generate
        i_filt: entity work.lp_filter
        port map (
            clock     => sys_clock,
            reset     => sys_reset,
            signal_in => sys_data,
            low_pass  => sys_filtered );
    end generate;
    r_no_filt: if not g_apply_filter generate
        sys_filtered <= sys_data;
    end generate;    

    i_sync: entity work.synchronizer_gzw
    generic map (
        g_width     => 18,
        g_fast      => false
    )
    port map(
        tx_clock    => sys_clock,
        tx_push     => sys_tick,
        tx_data     => std_logic_vector(sys_filtered),
        tx_done     => open,
        rx_clock    => audio_clock,
        rx_new_data => open,
        rx_data     => audio_data_i
    );
    
    audio_data <= signed(audio_data_i);
end architecture;

