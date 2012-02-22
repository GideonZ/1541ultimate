library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity logic_analyzer is
generic (
    g_timer_div    : positive := 50;
    g_change_width : positive := 16;
    g_data_length  : positive := 4 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    ev_dav      : in  std_logic;
    ev_data     : in  std_logic_vector(g_data_length*8-1 downto 0);
    
    ---
    mem_req     : out t_mem_req;
    mem_resp    : in  t_mem_resp;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp );
    
end logic_analyzer;

architecture gideon of logic_analyzer is
    signal enable_log   : std_logic;
    signal ev_timer     : integer range 0 to g_timer_div-1;
    signal ev_tick      : std_logic;
    signal ev_data_c    : std_logic_vector(g_data_length*8-1 downto 0);
    signal ev_data_d    : std_logic_vector(g_data_length*8-1 downto 0);
    signal ev_wdata     : std_logic_vector((g_data_length+2)*8 -1 downto 0);
    signal ev_addr      : unsigned(23 downto 0);
    signal stamp        : unsigned(14 downto 0);
    signal cnt          : integer range 0 to g_data_length+1;
    type t_state is (idle, writing);
    signal state        : t_state;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if ev_timer = 0 then
                ev_tick <= '1';
                ev_timer <= g_timer_div - 1;
            else
                ev_tick <= '0';
                ev_timer <= ev_timer - 1;
            end if;

            if ev_tick = '1' then
                if stamp /= 32766 then
                    stamp <= stamp + 1;
                end if;
            end if;

            ev_data_c <= ev_data;

            case state is
            when idle =>
                if enable_log = '1' then
                    if ev_dav='1' or ev_tick='1' then
                        if (ev_data_c(g_change_width-1 downto 0) /= ev_data_d(g_change_width-1 downto 0)) or (ev_dav = '1') then
                            ev_wdata <= ev_data_c & ev_dav & std_logic_vector(stamp);
                            stamp <= (others => '0');
                            cnt   <= 0;
                            state <= writing;
                        end if;
                        if ev_tick='1' and ev_dav='0' then
                            ev_data_d <= ev_data_c;
                        end if;
                    end if;
                end if;
                
            when writing =>
                mem_req.data    <= ev_wdata(cnt*8+7 downto cnt*8);
                mem_req.request <= '1';
                if mem_resp.rack='1' and mem_resp.rack_tag=X"F0" then
                    ev_addr <= ev_addr + 1;
                    mem_req.request <= '0';
                    if (cnt = g_data_length+1) then
                        state <= idle;
                    else
                        cnt <= cnt + 1;
                    end if;
                end if;

            when others =>
                null;
            end case;

            io_resp <= c_io_resp_init;

            if io_req.read='1' then
                io_resp.ack <= '1';
                case io_req.address(2 downto 0) is
                when "000" =>
                    io_resp.data <= std_logic_vector(ev_addr(7 downto 0));
                when "001" =>
                    io_resp.data <= std_logic_vector(ev_addr(15 downto 8));
                when "010" =>
                    io_resp.data <= std_logic_vector(ev_addr(23 downto 16));
                when "011" =>
                    io_resp.data <= "00000001";
                when "100" =>
                    io_resp.data <= std_logic_vector(to_unsigned(g_data_length+2, 8));
                when others =>
                    null;
                end case;
            elsif io_req.write='1' then
                io_resp.ack <= '1';
                if io_req.data = X"33" then
                    ev_addr <= (others => '0');
                    ev_data_d <= (others => '0'); -- to trigger first entry
                    stamp <= (others => '0');
                    enable_log <= '1';
                elsif io_req.data = X"44" then
                    enable_log <= '0';
                end if;
            end if;

            if reset='1' then
                state      <= idle;
                enable_log <= '0';
                cnt        <= 0;
                ev_timer   <= 0;
                mem_req.request <= '0';
                mem_req.data    <= (others => '0');
                ev_addr    <= (others => '0');
                stamp      <= (others => '0');
                ev_data_c  <= (others => '0');
                ev_data_d  <= (others => '0');
            end if;
        end if;
    end process;

    mem_req.tag         <= X"F0";
    mem_req.address     <= "01" & unsigned(ev_addr);
    mem_req.read_writen <= '0'; -- write only
end gideon;
