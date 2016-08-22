library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

use work.io_bus_pkg.all;

entity update_io is
port (
	clock       : in  std_logic                     := '0';             --       clock.clk
	reset       : in  std_logic                     := '0';             --       reset.reset

    slow_clock  : in  std_logic;
    slow_reset  : in  std_logic;

    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

    flash_selck : out std_logic;
    flash_sel   : out std_logic );
    
end entity update_io;

architecture rtl of update_io is
	component update_remote_update_0 is
		port (
			busy        : out std_logic;                                        -- busy
			data_out    : out std_logic_vector(28 downto 0);                    -- data_out
			param       : in  std_logic_vector(2 downto 0)  := (others => 'X'); -- param
			read_param  : in  std_logic                     := 'X';             -- read_param
			reconfig    : in  std_logic                     := 'X';             -- reconfig
			reset_timer : in  std_logic                     := 'X';             -- reset_timer
			read_source : in  std_logic_vector(1 downto 0)  := (others => 'X'); -- read_source
			clock       : in  std_logic                     := 'X';             -- clk
			reset       : in  std_logic                     := 'X'              -- reset
		);
	end component update_remote_update_0;

    signal busy        : std_logic;                                        -- busy
    signal data_out    : std_logic_vector(28 downto 0);                    -- data_out
    signal param       : std_logic_vector(2 downto 0)  := (others => '0'); -- param
    signal read_param  : std_logic                     := '0';             -- read_param
    signal reconfig    : std_logic                     := '0';             -- reconfig
    signal reset_timer : std_logic                     := '0';             -- reset_timer
    signal read_source : std_logic_vector(1 downto 0)  := (others => '0'); -- read_source
    signal flash_sel_i  : std_logic;
    signal flash_selck_i : std_logic;

    signal slow_req     : t_io_req;
    signal slow_resp    : t_io_resp;
begin
    i_bridge: entity work.io_bus_bridge2
    generic map (
        g_addr_width => 4
    )
    port map(
        clock_a      => clock,
        reset_a      => reset,
        req_a        => io_req,
        resp_a       => io_resp,
        clock_b      => slow_clock,
        reset_b      => slow_reset,
        req_b        => slow_req,
        resp_b       => slow_resp );
    

	remote_update_0 : component update_remote_update_0
		port map (
			busy        => busy,        --        busy.busy
			data_out    => data_out,    --    data_out.data_out
			param       => param,       --       param.param
			read_param  => read_param,  --  read_param.read_param
			reconfig    => reconfig,    --    reconfig.reconfig
			reset_timer => reset_timer, -- reset_timer.reset_timer
			read_source => read_source, -- read_source.read_source
			clock       => slow_clock,       --       clock.clk
			reset       => slow_reset        --       reset.reset
		);

    process(slow_clock)
        variable local  : unsigned(3 downto 0);
    begin
        if rising_edge(slow_clock) then
            local := slow_req.address(3 downto 0);
            slow_resp <= c_io_resp_init;
            read_param <= '0';
            
            if slow_req.read = '1' then
                slow_resp.ack <= '1';
                case local is
                when X"0" =>
                    slow_resp.data <= data_out(7 downto 0);
                when X"1" =>
                    slow_resp.data <= data_out(15 downto 8);
                when X"2" =>
                    slow_resp.data <= data_out(23 downto 16);
                when X"3" =>
                    slow_resp.data <= "000" & data_out(28 downto 24);
                when X"5" =>
                    slow_resp.data(0) <= busy;        
                when X"6" =>
                    slow_resp.data(0) <= reconfig;        
                when X"8"|X"9" =>
                    slow_resp.data(0) <= flash_selck_i;
                when X"A"|X"B" =>
                    slow_resp.data(0) <= flash_sel_i;
                when others =>
                    null;
                end case;
            end if;
            if slow_req.write = '1' then
                slow_resp.ack <= '1';
                case local is
                when X"4" =>
                    param <= slow_req.data(param'range);
                    read_source <= slow_req.data(5 downto 4);
                    read_param <= '1';
                when X"6" =>
                    if slow_req.data = X"BE" then
                        reconfig <= '1';
                    end if;
                when X"8" =>
                    flash_selck_i <= '0';
                when X"9" =>
                    flash_selck_i <= '1';
                when X"A" =>
                    flash_sel_i <= '0';
                when X"B" =>
                    flash_sel_i <= '1';
                when others =>
                    null;
                end case;
            end if;
            if slow_reset = '1' then
                read_source <= (others => '0');
                reconfig <= '0';
                param <= (others => '0');
                flash_selck_i <= '0';
                flash_sel_i <= '0';
            end if;
        end if;
    end process;

    flash_selck <= flash_selck_i;
    flash_sel   <= flash_sel_i;
end architecture rtl; -- of update
