-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 16 bit (burst of 4), externally 8x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use ieee.vital_timing.all;

library work;
    use work.mem_bus_pkg.all;

entity ext_mem_ctrl_v7_tb is

end ext_mem_ctrl_v7_tb;

architecture tb of ext_mem_ctrl_v7_tb is
    signal clock       : std_logic := '1';
    signal clk_2x      : std_logic := '1';
    signal reset       : std_logic := '0';
    signal inhibit     : std_logic := '0';
    signal is_idle     : std_logic;
    signal req         : t_mem_burst_32_req := c_mem_burst_32_req_init;
    signal resp        : t_mem_burst_32_resp;
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

    signal dummy_data  : std_logic_vector(15 downto 0) := (others => 'H');
    signal dummy_dqm   : std_logic_vector(1 downto 0) := (others => 'H');
    constant c_wire_delay : VitalDelayType01 := ( 2 ns, 3 ns );
begin

    clock <= not clock after 12 ns;
    clk_2x <= not clk_2x after 6 ns;
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.ext_mem_ctrl_v7
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
    
    	SDRAM_CLK	=> SDRAM_CLK,
    	SDRAM_CKE	=> SDRAM_CKE,
        SDRAM_CSn   => SDRAM_CSn,
    	SDRAM_RASn  => SDRAM_RASn,
    	SDRAM_CASn  => SDRAM_CASn,
    	SDRAM_WEn   => SDRAM_WEn,
        SDRAM_DQM   => SDRAM_DQM,
    
        SDRAM_BA    => SDRAM_BA,
        SDRAM_A     => SDRAM_A,
        SDRAM_DQ    => SDRAM_DQ );


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
        
        -- simple write / readback test
        queue_req('0', 0, 0, 0);
        queue_req('1', 0, 0, 0);

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
        variable v_data : unsigned(31 downto 0) := X"DEAD4001";
    begin
        if rising_edge(clock) then
            if resp.wdata_full='0' and reset='0' then
                req.data_push <= '1';
                req.data <= std_logic_vector(v_data);
                req.byte_en <= "0111";
                v_data := v_data + 1;
            else
                req.data_push <= '0';
            end if;
        end if;            
    end process;

    i_sdram : entity work.mt48lc16m16a2
    generic map(
        tipd_BA0                 => c_wire_delay,
        tipd_BA1                 => c_wire_delay,
        tipd_DQMH                => c_wire_delay,
        tipd_DQML                => c_wire_delay,
        tipd_DQ0                 => c_wire_delay,
        tipd_DQ1                 => c_wire_delay,
        tipd_DQ2                 => c_wire_delay,
        tipd_DQ3                 => c_wire_delay,
        tipd_DQ4                 => c_wire_delay,
        tipd_DQ5                 => c_wire_delay,
        tipd_DQ6                 => c_wire_delay,
        tipd_DQ7                 => c_wire_delay,
        tipd_DQ8                 => c_wire_delay,
        tipd_DQ9                 => c_wire_delay,
        tipd_DQ10                => c_wire_delay,
        tipd_DQ11                => c_wire_delay,
        tipd_DQ12                => c_wire_delay,
        tipd_DQ13                => c_wire_delay,
        tipd_DQ14                => c_wire_delay,
        tipd_DQ15                => c_wire_delay,
        tipd_CLK                 => c_wire_delay,
        tipd_CKE                 => c_wire_delay,
        tipd_A0                  => c_wire_delay,
        tipd_A1                  => c_wire_delay,
        tipd_A2                  => c_wire_delay,
        tipd_A3                  => c_wire_delay,
        tipd_A4                  => c_wire_delay,
        tipd_A5                  => c_wire_delay,
        tipd_A6                  => c_wire_delay,
        tipd_A7                  => c_wire_delay,
        tipd_A8                  => c_wire_delay,
        tipd_A9                  => c_wire_delay,
        tipd_A10                 => c_wire_delay,
        tipd_A11                 => c_wire_delay,
        tipd_A12                 => c_wire_delay,
        tipd_WENeg               => c_wire_delay,
        tipd_RASNeg              => c_wire_delay,
        tipd_CSNeg               => c_wire_delay,
        tipd_CASNeg              => c_wire_delay,
        -- tpd delays
        tpd_CLK_DQ2              => ( 4 ns, 4 ns, 4 ns, 4 ns, 4 ns, 4 ns ),
        tpd_CLK_DQ3              => ( 4 ns, 4 ns, 4 ns, 4 ns, 4 ns, 4 ns ),
--        -- tpw values: pulse widths
--        tpw_CLK_posedge          : VitalDelayType    := UnitDelay;
--        tpw_CLK_negedge          : VitalDelayType    := UnitDelay;
--        -- tsetup values: setup times
--        tsetup_DQ0_CLK           : VitalDelayType    := UnitDelay;
--        -- thold values: hold times
--        thold_DQ0_CLK            : VitalDelayType    := UnitDelay;
--        -- tperiod_min: minimum clock period = 1/max freq
--        tperiod_CLK_posedge      : VitalDelayType    := UnitDelay;
--
        mem_file_name => "none",
        tpowerup => 100 ns )
    port map(
        BA0     => SDRAM_BA(0),
        BA1     => SDRAM_BA(1),
        DQMH    => dummy_dqm(1),
        DQML    => SDRAM_DQM,
        DQ0     => SDRAM_DQ(0),
        DQ1     => SDRAM_DQ(1),
        DQ2     => SDRAM_DQ(2),
        DQ3     => SDRAM_DQ(3),
        DQ4     => SDRAM_DQ(4),
        DQ5     => SDRAM_DQ(5),
        DQ6     => SDRAM_DQ(6),
        DQ7     => SDRAM_DQ(7),
        DQ8     => dummy_data(8),
        DQ9     => dummy_data(9),
        DQ10    => dummy_data(10),
        DQ11    => dummy_data(11),
        DQ12    => dummy_data(12),
        DQ13    => dummy_data(13),
        DQ14    => dummy_data(14),
        DQ15    => dummy_data(15),
        CLK     => SDRAM_CLK,
        CKE     => SDRAM_CKE,
        A0      => SDRAM_A(0),
        A1      => SDRAM_A(1),
        A2      => SDRAM_A(2),
        A3      => SDRAM_A(3),
        A4      => SDRAM_A(4),
        A5      => SDRAM_A(5),
        A6      => SDRAM_A(6),
        A7      => SDRAM_A(7),
        A8      => SDRAM_A(8),
        A9      => SDRAM_A(9),
        A10     => SDRAM_A(10),
        A11     => SDRAM_A(11),
        A12     => SDRAM_A(12),
        WENeg   => SDRAM_WEn,
        RASNeg  => SDRAM_RASn,
        CSNeg   => SDRAM_CSn,
        CASNeg  => SDRAM_CASn );

end;
