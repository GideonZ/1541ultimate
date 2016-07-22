-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 32 bit (single beat), externally 4x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;


library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    use work.io_bus_bfm_pkg.all;
    
entity ddr2_ctrl_tb is

end entity;

architecture tb of ddr2_ctrl_tb is
    signal ref_clock   : std_logic := '1';
    signal ref_reset   : std_logic := '0';
    signal clock       : std_logic := '1';
    signal reset       : std_logic := '0';
    signal inhibit     : std_logic := '0';
    signal is_idle     : std_logic;
    signal req         : t_mem_req_32;
    signal resp        : t_mem_resp_32;

    signal io_req      : t_io_req;
    signal io_resp     : t_io_resp;

    signal correct     : std_logic;
    
	signal SDRAM_CLK   : std_logic;
    signal SDRAM_CLKn  : std_logic;
	signal SDRAM_CKE   : std_logic;
    signal SDRAM_CSn   : std_logic := '1';
	signal SDRAM_RASn  : std_logic := '1';
	signal SDRAM_CASn  : std_logic := '1';
	signal SDRAM_WEn   : std_logic := '1';
    signal SDRAM_DQM   : std_logic := '0';
    signal SDRAM_ODT   : std_logic := '0';
    signal SDRAM_DM    : std_logic := '0';
    signal SDRAM_DQS   : std_logic := '0';
    signal SDRAM_A     : std_logic_vector(13 downto 0);
    signal SDRAM_BA    : std_logic_vector(1 downto 0);
    signal SDRAM_DQ    : std_logic_vector(7 downto 0) := (others => 'Z');

	signal logic_CLK   : std_logic;
    signal logic_CLKn  : std_logic;
	signal logic_CKE   : std_logic;
    signal logic_CSn   : std_logic := '1';
	signal logic_RASn  : std_logic := '1';
	signal logic_CASn  : std_logic := '1';
	signal logic_WEn   : std_logic := '1';
    signal logic_DQM   : std_logic := '0';
    signal logic_ODT   : std_logic := '0';
    signal logic_A     : std_logic_vector(13 downto 0);
    signal logic_BA    : std_logic_vector(1 downto 0);
    
    constant g_in_delay : time := 0.3 ns;
begin
    ref_clock <= not ref_clock after 10 ns; -- 50 MHz reference clock
    ref_reset <= '1', '0' after 100 ns;

    i_bfm: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm" )
    port map (
        clock       => clock,
    
        req         => io_req,
        resp        => io_resp );

    i_mut: entity work.ddr2_ctrl
    port map (
        ref_clock         => ref_clock,
        ref_reset         => ref_reset,
        sys_clock_o       => clock,
        sys_reset_o       => reset,
        clock             => clock,
        reset             => reset,
        io_req            => io_req,
        io_resp           => io_resp,
        inhibit           => inhibit,
        is_idle           => is_idle,
        req               => req,
        resp              => resp,

        SDRAM_CLK         => logic_CLK,
        SDRAM_CLKn        => logic_CLKn,
        SDRAM_CKE         => logic_CKE,
        SDRAM_ODT         => logic_ODT,
        SDRAM_CSn         => logic_CSn,
        SDRAM_RASn        => logic_RASn,
        SDRAM_CASn        => logic_CASn,
        SDRAM_WEn         => logic_WEn,
        SDRAM_A           => logic_A,
        SDRAM_BA          => logic_BA,
        SDRAM_DM          => SDRAM_DM,
        SDRAM_DQ          => SDRAM_DQ,
        SDRAM_DQS         => SDRAM_DQS
    );

    SDRAM_A     <= transport logic_A    after 11.5 ns;
	SDRAM_CLK	<= transport logic_CLK  after 11.5 ns;
    SDRAM_CLKn  <= transport logic_CLKn after 11.5 ns;
	SDRAM_CKE	<= transport logic_CKE  after 11.5 ns;
    SDRAM_CSn   <= transport logic_CSn  after 11.5 ns;
	SDRAM_RASn  <= transport logic_RASn after 11.5 ns;
	SDRAM_CASn  <= transport logic_CASn after 11.5 ns;
	SDRAM_WEn   <= transport logic_WEn  after 11.5 ns;
    SDRAM_ODT   <= transport logic_ODT  after 11.5 ns;

    p_test: process
    begin
        req <= c_mem_req_32_init;

        wait until reset='0';
        wait for 2 us;
        wait until clock='1';
        
        req.read_writen <= '1'; -- read
        req.read_writen <= '0'; -- write
        req.request <= '1';
        req.data <= X"44332211";
        req.byte_en <= "0111";
        req.tag <= X"34";
        
        while true loop
            wait until clock='1';
            if resp.rack='1' then
                if req.read_writen = '0' then
                    req.address <= req.address + 4;
                end if;
                req.read_writen <= not req.read_writen;
            end if;
        end loop;
        
        wait;
    end process;

    process(SDRAM_CLK)
        variable delay : integer := -10;
    begin
        if rising_edge(SDRAM_CLK) then
            if SDRAM_CSn = '0' and
               SDRAM_RASn = '1' and
               SDRAM_CASn = '0' and
               SDRAM_WEn = '1' then -- read!
                delay := 7;
            end if;
        end if;
        delay := delay - 1;
        case delay is
        when 0 =>
            SDRAM_DQ <= transport X"22" after g_in_delay;
        when -1 =>
            SDRAM_DQ <= transport X"33" after g_in_delay;
        when -2 =>
            SDRAM_DQ <= transport X"44" after g_in_delay;
        when -3 =>
            SDRAM_DQ <= transport X"55" after g_in_delay;
        when others =>
            SDRAM_DQ <= transport "ZZZZZZZZ" after g_in_delay;
        end case;
    end process;

    p_io: process
        variable io : p_io_bus_bfm_object;
        variable data : std_logic_vector(7 downto 0);
        constant c_mr : std_logic_vector(13 downto 0) :=
            '0' & -- reserved
            '0' & -- normal PD
            "001" & -- Write recovery = 2;
            '0' & -- DLL reset = 0
            '0' & -- normal mode (non-test)
            "011" & -- CAS latency = 3
            '0' & -- sequential burst mode
            "010"; -- burst len = 4

        constant c_cmd_inactive  : std_logic_vector(3 downto 0) := "1111";
        constant c_cmd_nop       : std_logic_vector(3 downto 0) := "0111";
        constant c_cmd_active    : std_logic_vector(3 downto 0) := "0011";
        constant c_cmd_read      : std_logic_vector(3 downto 0) := "0101";
        constant c_cmd_write     : std_logic_vector(3 downto 0) := "0100";
        constant c_cmd_bterm     : std_logic_vector(3 downto 0) := "0110";
        constant c_cmd_precharge : std_logic_vector(3 downto 0) := "0010";
        constant c_cmd_refresh   : std_logic_vector(3 downto 0) := "0001";
        constant c_cmd_mode_reg  : std_logic_vector(3 downto 0) := "0000";

    begin
        wait until reset='0';
        bind_io_bus_bfm("io_bfm", io);

        wait for 1 us;
        io_write(io, X"0C", X"01"); -- enable clock

        io_write(io, X"00", X"00");
        io_write(io, X"01", X"04");
        io_write(io, X"02", X"0" & c_cmd_precharge);
        
        io_write(io, X"00", c_mr(7 downto 0));
        io_write(io, X"01", "00" & c_mr(13 downto 8));
        io_write(io, X"02", X"0" & c_cmd_mode_reg);

        for i in 1 to 80 loop
            io_write(io, X"09", X"35"); -- make a step on the read clock
            wait for 16*7 ns;
        end loop;            
        wait;
        
    end process;
    
    p_check: process(clock)
    begin
        if rising_edge(clock) then
            if resp.dack_tag = X"34" then
                if resp.data = X"55443322" then
                    correct <= '1';
                else
                    correct <= '0';
                end if;
            end if;
        end if;
    end process;
    
end;
