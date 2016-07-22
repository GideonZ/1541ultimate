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

entity ext_mem_ctrl_v5_tb is

end ext_mem_ctrl_v5_tb;

architecture tb of ext_mem_ctrl_v5_tb is
    signal clock       : std_logic := '1';
    signal clk_2x      : std_logic := '1';
    signal reset       : std_logic := '0';
    signal inhibit     : std_logic := '0';
    signal is_idle     : std_logic;
    signal req         : t_mem_req_32;
    signal resp        : t_mem_resp_32;
	signal SDRAM_CLK   : std_logic;
	signal SDRAM_CKE   : std_logic;
    signal SDRAM_CSn   : std_logic := '1';
	signal SDRAM_RASn  : std_logic := '1';
	signal SDRAM_CASn  : std_logic := '1';
	signal SDRAM_WEn   : std_logic := '1';
    signal SDRAM_DQM   : std_logic := '0';
    signal SDRAM_A     : std_logic_vector(12 downto 0);
    signal SDRAM_BA    : std_logic_vector(1 downto 0);
    signal SDRAM_DQ    : std_logic_vector(7 downto 0) := (others => 'Z');

	signal logic_CLK   : std_logic;
	signal logic_CKE   : std_logic;
    signal logic_CSn   : std_logic := '1';
	signal logic_RASn  : std_logic := '1';
	signal logic_CASn  : std_logic := '1';
	signal logic_WEn   : std_logic := '1';
    signal logic_DQM   : std_logic := '0';
    signal logic_A     : std_logic_vector(12 downto 0);
    signal logic_BA    : std_logic_vector(1 downto 0);
    
    signal Q           : std_logic_vector(7 downto 0);
    signal Qd          : std_logic_vector(7 downto 0);
begin

    clock <= not clock after 10 ns;
    clk_2x <= not clk_2x after 5 ns;
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.ext_mem_ctrl_v5
	generic map (
		g_simulation => true )
    port map (
        clock       => clock,
        clk_2x      => clk_2x,
        reset       => reset,
    
        inhibit     => inhibit,
        is_idle     => is_idle,
    
        req         => req,
        resp        => resp,
    
    	SDRAM_CLK	=> logic_CLK,
    	SDRAM_CKE	=> logic_CKE,
        SDRAM_CSn   => logic_CSn,
    	SDRAM_RASn  => logic_RASn,
    	SDRAM_CASn  => logic_CASn,
    	SDRAM_WEn   => logic_WEn,
        SDRAM_DQM   => logic_DQM,
    
        SDRAM_A     => logic_A,
        SDRAM_BA    => logic_BA,
        SDRAM_DQ    => SDRAM_DQ );


    SDRAM_A     <= transport logic_A    after 6 ns;
	SDRAM_CLK	<= transport logic_CLK  after 6 ns;
	SDRAM_CKE	<= transport logic_CKE  after 6 ns;
    SDRAM_CSn   <= transport logic_CSn  after 6 ns;
	SDRAM_RASn  <= transport logic_RASn after 6 ns;
	SDRAM_CASn  <= transport logic_CASn after 6 ns;
	SDRAM_WEn   <= transport logic_WEn  after 6 ns;
    SDRAM_DQM   <= transport logic_DQM  after 6 ns;

    p_test: process
    begin
        req <= c_mem_req_32_init;

        wait until reset='0';
        wait until clock='1';
        
        req.read_writen <= '1'; -- read
        req.read_writen <= '0'; -- write
        req.request <= '1';
        req.size <= '1';
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

    p_read: process(SDRAM_CLK)
        variable count : integer := 10;
    begin
        if rising_edge(SDRAM_CLK) then
            if SDRAM_CSn='0' and SDRAM_RASn='1' and SDRAM_CASn='0' and SDRAM_WEn='1' then -- start read
                count := 0;
            end if;
           
            case count is
            when 0 =>
                Q <= X"01";
            when 1 =>
                Q <= X"02";
            when 2 =>
                Q <= X"03";
            when 3 =>
                Q <= X"04";
            when others =>
                Q <= (others => 'Z');           
            end case;

            Qd <= Q;
            if Qd(0)='Z' then
                SDRAM_DQ <= Qd after 3.6 ns;
            else
                SDRAM_DQ <= Qd after 5.6 ns;
            end if;
            
            count := count + 1;                
        end if;
    end process;

end;
