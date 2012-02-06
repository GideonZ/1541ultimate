-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 16 bit (burst of 4), externally 8x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;


library work;
    use work.mem_bus_pkg.all;

entity ext_mem_ctrl_v6_tb is

end ext_mem_ctrl_v6_tb;

architecture tb of ext_mem_ctrl_v6_tb is
    signal clock       : std_logic := '1';
    signal clk_2x      : std_logic := '1';
    signal reset       : std_logic := '0';
    signal inhibit     : std_logic := '0';
    signal is_idle     : std_logic;
    signal req         : t_mem_burst_16_req := c_mem_burst_16_req_init;
    signal resp        : t_mem_burst_16_resp;
	signal SDRAM_CLK   : std_logic;
	signal SDRAM_CKE   : std_logic;
    signal SDRAM_CSn   : std_logic := '1';
	signal SDRAM_RASn  : std_logic := '1';
	signal SDRAM_CASn  : std_logic := '1';
	signal SDRAM_WEn   : std_logic := '1';
    signal SDRAM_DQM   : std_logic := '0';
    signal SDRAM_A     : std_logic_vector(12 downto 0);
    signal SDRAM_BA    : std_logic_vector(1 downto 0);
    signal MEM_D       : std_logic_vector(7 downto 0) := (others => 'Z');

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
    signal start       : std_logic := '0';

    signal dummy_data  : std_logic_vector(15 downto 0) := (others => 'H');
    signal dummy_dqm   : std_logic_vector(1 downto 0) := (others => 'H');

begin

    clock <= not clock after 12 ns;
    clk_2x <= not clk_2x after 6 ns;
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.ext_mem_ctrl_v6
	generic map (
        q_tcko_data  => 5 ns,
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
    
        SDRAM_BA    => logic_BA,
        SDRAM_A     => logic_A,
        SDRAM_DQ    => MEM_D );


    p_test: process
        procedure queue_req(rw : std_logic; bank : integer; row : integer; col : integer) is
        begin
            req.request <= '1';
            req.read_writen <= rw;
            req.address <= to_unsigned(bank*8192 + col*8 + row*32768, req.address'length);
            wait for 2 ns;
            while resp.ready='0' loop
                wait until clock='1';
            end loop;
            wait until clock='1';
            req.request <= '0';
        end procedure;
    begin
        req.read_writen <= '1'; -- read
        req.request <= '0';
        req.address <= unsigned(to_signed(-32, req.address'length));
        req.data_pop <= '0';
        
        wait until reset='0';
        wait until clock='1';
        
        while true loop
            -- read-read, other row, other bank
            queue_req('1', 0, 16, 127);
            queue_req('1', 1, 17, 0);
            -- read-read, other row, same bank
            queue_req('1', 1, 18, 1);
            -- read-read, same row, other bank
            queue_req('1', 2, 18, 2);
            -- read-read, same row, same bank
            queue_req('1', 2, 18, 3);
            -- read-write, other row, other bank
            queue_req('0', 0, 16, 4);
            -- read-write, other row, same bank
            queue_req('1', 0, 16, 127);
            queue_req('0', 0, 17, 5);
            -- read-write, same row, other bank
            queue_req('1', 0, 18, 127);
            queue_req('0', 1, 18, 6);
            -- read-write, same row, same bank
            queue_req('1', 2, 19, 127);
            queue_req('0', 2, 19, 7);
            -- write-read, other row, other bank
            queue_req('1', 3, 20, 8);
            -- write-read, other row, same bank
            queue_req('0', 0, 16, 127);
            queue_req('1', 0, 17, 9);
            -- write-read, same row, other bank
            queue_req('0', 1, 18, 127);
            queue_req('1', 2, 18, 10);
            -- write-read, same row, same bank
            queue_req('0', 3, 19, 127);
            queue_req('1', 3, 19, 11);
            -- write-write, other row, other bank
            queue_req('0', 0, 20, 127);
            queue_req('0', 1, 21, 12);
            -- write-write, other row, same bank
            queue_req('0', 1, 22, 13);
            -- write-write, same row, other bank
            queue_req('0', 2, 22, 14);
            -- write-write, same row, same bank
            queue_req('0', 2, 22, 15);

            -- read write toggle performance tests..
            for i in 1 to 10 loop
                queue_req('1', 0, 0, i);
                queue_req('0', 1, 0, i);
            end loop;            

            for i in 1 to 10 loop
                queue_req('1', 0, 0, i);
                queue_req('0', 0, 0, i);
            end loop;            

            for i in 1 to 10 loop
                queue_req('1', 0, 0, i);
                queue_req('0', 0, 1, i);
            end loop;            

            for i in 1 to 1000 loop
                queue_req('1', 0, 0, i);
            end loop;
        end loop;
        
        wait;
    end process;

    p_write: process(clock)
        variable v_data : unsigned(15 downto 0) := X"4001";
    begin
        if rising_edge(clock) then
            if resp.wdata_full='0' and reset='0' then
                req.data_push <= '1';
                req.data <= std_logic_vector(v_data);
                req.byte_en <= "01";
                v_data := v_data + 1;
            else
                req.data_push <= '0';
            end if;
        end if;            
    end process;

    i_sdram : entity work.mt48lc16m16a2
    generic map(
        mem_file_name => "none",
        tpowerup => 100 ns )
    port map(
        BA0     => logic_BA(0),
        BA1     => logic_BA(1),
        DQMH    => dummy_dqm(1),
        DQML    => logic_DQM,
        DQ0     => MEM_D(0),
        DQ1     => MEM_D(1),
        DQ2     => MEM_D(2),
        DQ3     => MEM_D(3),
        DQ4     => MEM_D(4),
        DQ5     => MEM_D(5),
        DQ6     => MEM_D(6),
        DQ7     => MEM_D(7),
        DQ8     => dummy_data(8),
        DQ9     => dummy_data(9),
        DQ10    => dummy_data(10),
        DQ11    => dummy_data(11),
        DQ12    => dummy_data(12),
        DQ13    => dummy_data(13),
        DQ14    => dummy_data(14),
        DQ15    => dummy_data(15),
        CLK     => logic_CLK,
        CKE     => logic_CKE,
        A0      => logic_A(0),
        A1      => logic_A(1),
        A2      => logic_A(2),
        A3      => logic_A(3),
        A4      => logic_A(4),
        A5      => logic_A(5),
        A6      => logic_A(6),
        A7      => logic_A(7),
        A8      => logic_A(8),
        A9      => logic_A(9),
        A10     => logic_A(10),
        A11     => logic_A(11),
        A12     => logic_A(12),
        WENeg   => logic_WEn,
        RASNeg  => logic_RASn,
        CSNeg   => logic_CSn,
        CASNeg  => logic_CASn );

--    SDRAM_A     <= transport logic_A    after 6 ns;
--    SDRAM_BA    <= transport logic_BA   after 6 ns;
--    SDRAM_CLK   <= transport logic_CLK  after 6 ns;
--    SDRAM_CKE   <= transport logic_CKE  after 6 ns;
--    SDRAM_CSn   <= transport logic_CSn  after 6 ns;
--    SDRAM_RASn  <= transport logic_RASn after 6 ns;
--    SDRAM_CASn  <= transport logic_CASn after 6 ns;
--    SDRAM_WEn   <= transport logic_WEn  after 6 ns;
--    SDRAM_DQM   <= transport logic_DQM  after 6 ns;

--    p_ram_read_emu: process(SDRAM_CLK)
--        variable count : integer := 10;
--        variable ba : std_logic_vector(3 downto 0);
--    begin
--        if rising_edge(SDRAM_CLK) then
--            start <= '0';
--            Qd <= Q;
--            if SDRAM_CSn='0' and SDRAM_RASn='1' and SDRAM_CASn='0' and SDRAM_WEn='1' then -- start read
--                start <= '1';
--                ba := "00" & SDRAM_BA;
--                count := 0;
--            end if;
--           
--            case count is
--            when 0 =>
--                Q <= ba & X"1";
--            when 1 =>
--                Q <= ba & X"2";
--            when 2 =>
--                Q <= ba & X"3";
--            when 3 =>
--                Q <= ba & X"4";
--            when 4 =>
--                Q <= ba & X"5";
--            when 5 =>
--                Q <= ba & X"6";
--            when 6 =>
--                Q <= ba & X"7";
--            when 7 =>
--                Q <= ba & X"8";
--            when others =>
--                Q <= (others => 'Z');           
--            end case;
--
--            if Qd(0)='Z' then
--                MEM_D <= transport Qd after 3.6 ns;
--            else
--                MEM_D <= transport Qd after 5.6 ns;
--            end if;
--    
--            count := count + 1;                
--        end if;
--    end process;

end;
