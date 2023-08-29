--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: Bus functional model of a stream source
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.stream_source_pkg.all;

entity stream_source_bfm is
generic (
    g_name          : string;
    g_data_width    : natural;
    g_user_width    : natural
);
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    data        : out std_logic_vector(g_data_width-1 downto 0);
    user        : out std_logic_vector(g_user_width-1 downto 0);
    last        : out std_logic;
    valid       : out std_logic;
    ready       : in  std_logic );
end entity;

architecture bfm of stream_source_bfm is
    shared variable src : h_stream_source := 0;
    shared variable sp : p_stream_source := null;
    signal valid_i : std_logic;
begin
    process
    begin
        register_stream_source(g_name, src);
        sp := stream_sources(src);
        wait;
    end process;

    process(clock)
        variable d  : std_logic_vector(data'range);
        variable u  : std_logic_vector(user'range);
        variable l  : std_logic;
    begin
        if rising_edge(clock) then
            if reset = '1' then
                valid_i <= '0';

            else
                if ready = '1' then
                    valid_i <= '0';
                end if;
                if (ready = '1' or valid_i = '0') and sp.head /= null then
                    get(src, d, u, l);
                    data <= d;
                    user <= u;
                    last <= l;
                    valid_i <= '1';
                end if;
            end if;
        end if;
    end process;
    valid <= valid_i;
end architecture;