--------------------------------------------------------------------------------
-- This confidential and proprietary software may be used only as authorized by
-- a licensing agreement from Altera Corporation.
--
-- Legal Notice: (C)2010 Altera Corporation. All rights reserved.  Your
-- use of Altera Corporation's design tools, logic functions and other
-- software and tools, and its AMPP partner logic functions, and any
-- output files any of the foregoing (including device programming or
-- simulation files), and any associated documentation or information are
-- expressly subject to the terms and conditions of the Altera Program
-- License Subscription Agreement or other applicable license agreement,
-- including, without limitation, that your use is for the sole purpose
-- of programming logic devices manufactured by Altera and sold by Altera
-- or its authorized distributors.  Please refer to the applicable
-- agreement for further details.
--
-- The entire notice above must be reproduced on all authorized copies and any
-- such reproduction must be pursuant to a licensing agreement from Altera.
--
-- Title        : Example top level testbench for ddr2 DDR/2/3 SDRAM High Performance Controller
-- Project      : DDR/2/3 SDRAM High Performance Controller
--
-- File         : ddr2_example_top_tb.vhd
--
-- Revision     : V14.1
--
-- Abstract:
-- Automatically generated testbench for the example top level design to allow
-- functional and timing simulation.
--
--------------------------------------------------------------------------------
--
-- *************** This is a MegaWizard generated file ****************
--
-- If you need to edit this file make sure the edits are not inside any 'MEGAWIZARD'
-- text insertion areas.
-- (between "<< START MEGAWIZARD INSERT" and "<< END MEGAWIZARD INSERT" comments)
--
-- Any edits inside these delimiters will be overwritten by the megawizard if you
-- re-run it.
--
-- If you really need to make changes inside these delimiters then delete
-- both 'START' and 'END' delimiters.  This will stop the megawizard updating this
-- section again.
--
------------------------------------------------------------------------------------
-- << START MEGAWIZARD INSERT PARAMETER_LIST
-- Parameters:
--
-- Device Family                      : cyclone iv e
-- local Interface Data Width         : 32
-- MEM_CHIPSELS                       : 1
-- MEM_CS_PER_RANK                    : 1
-- MEM_BANK_BITS                      : 2
-- MEM_ROW_BITS                       : 14
-- MEM_COL_BITS                       : 10
-- LOCAL_DATA_BITS                    : 32
-- NUM_CLOCK_PAIRS                    : 1
-- CLOCK_TICK_IN_PS                   : 6666
-- REGISTERED_DIMM                    : false
-- TINIT_CLOCKS                       : 15002
-- Data_Width_Ratio                   : 4

-- << END MEGAWIZARD INSERT PARAMETER_LIST
------------------------------------------------------------------------------------
-- << MEGAWIZARD PARSE FILE DDR14.1



library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.numeric_std.all;
use std.textio.all;

-- << START MEGAWIZARD INSERT ENTITY
entity ddr2_example_top_tb is
-- << END MEGAWIZARD INSERT ENTITY
    generic (

-- << START MEGAWIZARD INSERT GENERICS
        gMEM_CHIPSELS          : in integer := 1;
        gMEM_CS_PER_RANK       : in integer := 1;
        gMEM_NUM_RANKS         : in integer := 1 / 1;
        gMEM_BANK_BITS         : in integer := 2;
        gMEM_ROW_BITS          : in integer := 14;
        gMEM_COL_BITS          : in integer := 10;
        gMEM_ADDR_BITS         : in integer := 14;
        gMEM_DQ_PER_DQS        : in integer := 8;
        DWIDTH_RATIO	       : in integer := 4;
        DM_DQS_WIDTH	       : in integer := 1;
        gLOCAL_DATA_BITS       : in integer := 32;
        gLOCAL_IF_DWIDTH_AFTER_ECC : in integer := 32;
        gNUM_CLOCK_PAIRS       : in integer := 1;
        RTL_ROUNDTRIP_CLOCKS   : in real    := 0.0;
        CLOCK_TICK_IN_PS       : in integer := 6666;
        REGISTERED_DIMM        : in boolean := false;
        BOARD_DQS_DELAY        : in integer := 0;
        BOARD_CLK_DELAY        : in integer := 0;

        TINIT_CLOCKS           : in integer := 15002;
        REF_CLOCK_TICK_IN_PS   : in integer := 20000;
        -- Below 2 lines for SPR272543: 
        -- Testbench workaround for tests with "dedicated memory clock phase shift" failing, 
        -- because dqs delay isnt' being modelled in simulations
        gMEM_CLK_PHASE_EN       : in boolean := false;
        gMEM_CLK_PHASE          : in real    := 0.0;
        
        -- Parameters below are for generic memory model
        gMEM_TQHS_PS		: in integer := 340;
        gMEM_TAC_PS		: in integer := 450;
        gMEM_TDQSQ_PS		: in integer := 240;
        gMEM_IF_TRCD_NS		: in real := 15.0;
        gMEM_IF_TWTR_CK		: in integer := 3;
        gMEM_TDSS_CK		: in real := 0.2;
        gMEM_IF_TRFC_NS   	: in real := 105.0;
        gMEM_IF_TRP_NS   	: in real := 15.0;
	
-- << END MEGAWIZARD INSERT GENERICS

        RTL_DELAYS             : in integer := 1;  -- set to zero for Gatelevel
        USE_GENERIC_MEMORY_MODEL : in boolean := FALSE

    );
end;

-- << START MEGAWIZARD INSERT ARCHITECTURE
architecture rtl of ddr2_example_top_tb is
-- << END MEGAWIZARD INSERT ARCHITECTURE

    -------------------------------------------------------------------------------
    -- Functions needed in VHDL 
    function auk_to_integer (value: std_logic_vector) return integer is
    constant V: std_logic_vector(1 to value'length) := value;
    variable result: integer := 0;
    variable bit0: integer := 0;
    variable err: integer := 0;
    begin
        for i in 1 to value'length loop
            case V(i) is
            when '0' => bit0 := 0;
            when '1' => bit0 := 1;
            when others => err := 1;
            end case;
            result := (result * 2) + bit0;
        end loop;
        if (err = 0) then return result;
        else
            assert false report "auk_to_integer:: There is an 'U'|'X'|'W'|'Z'|'-' in an arithmetic operand, and it has been converted to 0." severity warning;
            return 0;
        end if;
    end auk_to_integer;

    function auk_to_string (value: integer; base: integer; size: integer) return string is
        variable V: integer := value;
        variable Result: string(1 to size);
        variable Width: natural := 0;
        constant MAX: INTEGER := 2147483647;
    begin
        assert ((base = 2) or (base=10) or (base=16))
            report "invalid base"
            severity ERROR;
        if V < 0 then
            V := (V + MAX) + 1;
        end if;
        for I in Result'Reverse_range loop
            case V mod base is
            when 0 => Result(I) := '0';
            when 1 => Result(I) := '1';
            when 2 => Result(I) := '2';
            when 3 => Result(I) := '3';
            when 4 => Result(I) := '4';
            when 5 => Result(I) := '5';
            when 6 => Result(I) := '6';
            when 7 => Result(I) := '7';
            when 8 => Result(I) := '8';
            when 9 => Result(I) := '9';
            when 10 => Result(I) := 'a';
            when 11 => Result(I) := 'b';
            when 12 => Result(I) := 'c';
            when 13 => Result(I) := 'd';
            when 14 => Result(I) := 'e';
            when 15 => Result(I) := 'f';
            when others =>
                      Result(I) := '?';
            end case;
            if V > 0 then
                Width := Width + 1;
            end if;
            V := V / base;
        end loop;
        if value < 0 then
            Result(Result'Length - Width) := '-';
            Width := Width + 1;
        end if;
        if Width = 0 then
            Width := 1;
        end if;
        -- pad to at least size wide
        if (Width < size) then
            Width := size;
        end if;

        return Result(Result'Length - Width + 1 to Result'Length);
    end auk_to_string;
    
    function auk_to_string (value: std_logic_vector; base: integer; size: integer) return string is
        variable ivalue : integer;
    begin
        ivalue := auk_to_integer(value);
        return auk_to_string(ivalue, base, size);
    end auk_to_string;

    -- override the "=" function because it doesn't work very well when comparing 'Z's
    function "=" (a, b : std_logic_vector) return boolean is
        variable a_bit, b_bit : std_logic;
        variable result : boolean;
    begin
        result := true;

        for i in a'reverse_range loop
            a_bit := a(i);
            b_bit := b(i);

            if (a_bit /= b_bit) then
                result := false;
            end if;

        end loop;

        return result;
    end; -- overridden "=" function

    function bool_to_integer (a : boolean)  return integer is
    begin
        if (a = true) then
            return 1;
        else
            return 0;
        end if;
    end;

    
    
    -------------------------------------------------------------------------------
    -- Component for the generic memory model - you should replace this with the model you are using
    component ddr2_mem_model is
    port(
        signal mem_clk             : in     std_logic;
        signal mem_clk_n           : in     std_logic;
        signal mem_cke             : in     std_logic;
        signal mem_cs_n            : in     std_logic;
        signal mem_ras_n           : in     std_logic;
        signal mem_cas_n           : in     std_logic;
        signal mem_we_n            : in     std_logic;
        signal mem_dm              : in     std_logic;
        signal mem_ba              : in     std_logic_vector;
        signal mem_addr            : in     std_logic_vector;
        signal mem_dq              : inout  std_logic_vector;
        signal mem_dqs             : inout  std_logic;
        signal mem_dqs_n           : inout  std_logic;
        signal mem_odt             : in     std_logic
    );
    end component ddr2_mem_model;

    
    -------------------------------------------------------------------------------


    -- Delay the incoming DQ & DQS to mimic the SDRAM round trip delay
    -- The round trip delay is now modeled inside the datapath (<your core name>_auk_ddr_dqs_group.v/vhd) for RTL simulation.
    constant SAMPLE_DELAY : integer := 0; -- RTL only

    constant GATE_BOARD_DQS_DELAY  : integer := BOARD_DQS_DELAY * abs(RTL_DELAYS-1);            -- Gate level timing only
    constant GATE_BOARD_CLK_DELAY  : integer := BOARD_CLK_DELAY * abs(RTL_DELAYS-1);            -- Gate level timing only
    
    -- Parameters below are for generic memory model
    constant gMEM_IF_TRCD_PS  	   : real := gMEM_IF_TRCD_NS * 1000.0;
    constant gMEM_IF_TWTR_PS  	   : integer := gMEM_IF_TWTR_CK * CLOCK_TICK_IN_PS;
    constant gMEM_IF_TRFC_PS  	   : real := gMEM_IF_TRFC_NS * 1000.0;
    constant gMEM_IF_TRP_PS        : real := gMEM_IF_TRP_NS * 1000.0;
    constant CLOCK_TICK_IN_NS 	   : real := real(CLOCK_TICK_IN_PS) / 1000.0;
    constant gMEM_TDQSQ_NS  	   : real := real(gMEM_TDQSQ_PS) * 1000.0;
    constant gMEM_TDSS_NS 	   : real := gMEM_TDSS_CK * CLOCK_TICK_IN_NS;
    
    -- Below 2 lines for SPR272543: 
    -- Testbench workaround for tests with "dedicated memory clock phase shift" failing, 
    -- because dqs delay isnt being modelled in simulations
    constant MEM_CLK_RATIO         : real := (360.0 - gMEM_CLK_PHASE)/360.0;            -- Gate level timing only
    constant MEM_CLK_DELAY         : integer :=  integer(MEM_CLK_RATIO * real(CLOCK_TICK_IN_PS)) * bool_to_integer(gMEM_CLK_PHASE_EN);

    signal cmd_bus_watcher_enabled  : std_logic := '0';

    signal   clk         : std_logic := '0';
    signal   clk_n       : std_logic := '1';

    signal   reset_n        : std_logic;
    signal   mem_reset_n        : std_logic;
    signal   a              : std_logic_vector(gMEM_ADDR_BITS - 1 downto 0);
    signal   ba             : std_logic_vector(gMEM_BANK_BITS - 1 downto 0);
    signal   cs_n           : std_logic_vector(gMEM_CHIPSELS - 1 downto 0);
    signal   cke            : std_logic_vector(gMEM_NUM_RANKS - 1 downto 0);
    signal   odt            : std_logic_vector(gMEM_NUM_RANKS - 1 downto 0);
    signal   ras_n          : std_logic;
    signal   cas_n          : std_logic;
    signal   we_n           : std_logic;
    
    -- DDR3 RDIMM only
    signal   mem_err_out_n  : std_logic;
    signal   mem_ac_parity  : std_logic;

    signal   dm             : std_logic_vector(gLOCAL_DATA_BITS / DWIDTH_RATIO / gMEM_DQ_PER_DQS - 1 downto 0);

    -- signal stratix_dqs_ref_clk    : std_logic;  -- only used on stratix to provide external dll reference clock

    signal clk_to_sdram    : std_logic_vector(gNUM_CLOCK_PAIRS-1 downto 0);
    signal clk_to_sdram_n  : std_logic_vector(gNUM_CLOCK_PAIRS-1 downto 0);

    signal clk_to_ram      : std_logic;
    signal clk_to_ram_n    : std_logic;

    signal a_delayed       : std_logic_vector(gMEM_ROW_BITS - 1 downto 0);
    signal ba_delayed      : std_logic_vector(gMEM_BANK_BITS - 1 downto 0);
    signal cke_delayed     : std_logic_vector(gMEM_NUM_RANKS - 1 downto 0);
    signal odt_delayed     : std_logic_vector(gMEM_NUM_RANKS - 1 downto 0);
    signal cs_n_delayed    : std_logic_vector(gMEM_NUM_RANKS - 1 downto 0);
    signal ras_n_delayed   : std_logic;
    signal cas_n_delayed   : std_logic;
    signal we_n_delayed    : std_logic;
    signal dm_delayed      : std_logic_vector(gLOCAL_DATA_BITS / DWIDTH_RATIO / gMEM_DQ_PER_DQS - 1 downto 0);

    signal mem_dq         : std_logic_vector(gLOCAL_DATA_BITS / DWIDTH_RATIO - 1 downto 0) := (others => 'Z');
    signal mem_dqs        : std_logic_vector(gLOCAL_DATA_BITS / DWIDTH_RATIO / gMEM_DQ_PER_DQS - 1 downto 0) := (others => 'Z');
    signal mem_dqs_n      : std_logic_vector(gLOCAL_DATA_BITS / DWIDTH_RATIO / gMEM_DQ_PER_DQS - 1 downto 0) := (others => 'Z');
    signal tdqs_n_unused  : std_logic_vector(gLOCAL_DATA_BITS / DWIDTH_RATIO / gMEM_DQ_PER_DQS - 1 downto 0) := (others => 'Z');

    signal zero_one        : std_logic_vector(gMEM_BANK_BITS - 1 downto 0) := (others => '0'); 

    signal test_complete   : std_logic;
    signal test_status   : std_logic_vector(7 downto 0);
    -- counter to count the number of sucessful read and write loops
    signal test_complete_count : integer;

    signal pnf             : std_logic;
    signal pnf_per_byte    : std_logic_vector(gLOCAL_IF_DWIDTH_AFTER_ECC/8 - 1 downto 0);


begin
    mem_err_out_n <= '1';
    zero_one(0) <= '1';
    mem_dq <= (others => 'L');
    mem_dqs <= (others => 'L');
    mem_dqs_n <= (others => 'L');

-- << START MEGAWIZARD INSERT DUT_INSTANCE_NAME
dut : entity work.ddr2_example_top
-- << END MEGAWIZARD INSERT DUT_INSTANCE_NAME
    port map
    (
        -- clocks and reset
        clock_source       =>  clk,      --  PLD input clock source from which all clocks are derived.
        global_reset_n     => reset_n,

-- << START MEGAWIZARD INSERT PORT_MAP
        mem_clk    =>  clk_to_sdram, 
        mem_clk_n  =>  clk_to_sdram_n,
        -- ddr sdram interface
        mem_odt       => odt,
        mem_cke       => cke,
        mem_cs_n      => cs_n,
        mem_ras_n     => ras_n,
        mem_cas_n     => cas_n,
        mem_we_n      => we_n,
        mem_ba        => ba,
        mem_addr      => a,
        mem_dq        => mem_dq,
        mem_dqs       => mem_dqs,
	        mem_dm       => dm,

-- << END MEGAWIZARD INSERT PORT_MAP

        test_complete   => test_complete,
	test_status	=> test_status,
        pnf_per_byte    => pnf_per_byte,
        pnf             => pnf


    );

    -- Generic memory model instantiation - you must edit this to match the memory model that you are using 
            
                    ddr : ddr2_mem_model
                        port map (
                            mem_clk         => clk_to_ram,            
                            mem_clk_n       => clk_to_ram_n,          
                            mem_cke         => cke_delayed(0),        
                            mem_cs_n        => cs_n_delayed(0),       
                            mem_ras_n       => ras_n_delayed,         
                            mem_cas_n       => cas_n_delayed,         
                            mem_we_n        => we_n_delayed,          
                            mem_dm          => dm_delayed(0),                                                   
                            mem_ba          => ba_delayed,                                                      
                            mem_addr        => a_delayed,                           
                            mem_dq          => mem_dq,
                            mem_dqs         => mem_dqs(0),                                              
                            mem_dqs_n       => mem_dqs_n(0),
                            mem_odt         => odt_delayed(0)
                        );
            


    process
    begin
       clk <= '0';
       clk_n <= '1';
       while (true) loop
           --wait for 10 ns;
           wait for (REF_CLOCK_TICK_IN_PS/2) * 1 ps;
           clk <= not clk;
           clk_n <= not clk_n;
       end loop;
       wait;
    end process;


    -- Below 1 line updated for SPR272543: 
    -- Testbench workaround for tests with "dedicated memory clock phase shift" failing, 
    -- because dqs delay isnt being modelled in simulations
    clk_to_ram      <= transport clk_to_sdram(0)   after (GATE_BOARD_CLK_DELAY + MEM_CLK_DELAY) * 1 ps;
    clk_to_ram_n    <= NOT clk_to_ram;      -- mem model ignores clk_n ?

    process
    begin
        reset_n <= '0';
        wait until (clk'event);
        wait until (clk'event);
        wait until (clk'event);
        wait until (clk'event);
        wait until (clk'event);
        wait until (clk'event);
        reset_n <= '1';
        wait;
    end process;



    -- control and data lines = 3 inches
    a_delayed       <=   transport a(14 - 1 downto 0)      after GATE_BOARD_CLK_DELAY * 1 ps + 1 ps;
    ba_delayed      <=   transport ba     after GATE_BOARD_CLK_DELAY * 1 ps + 1 ps;
    cke_delayed     <=   transport cke    after GATE_BOARD_CLK_DELAY * 1 ps + 1 ps;
    odt_delayed     <=   transport odt    after GATE_BOARD_CLK_DELAY * 1 ps + 1 ps;  -- ddr2 only
    cs_n_delayed    <=   transport cs_n (gMEM_NUM_RANKS - 1 downto 0)  after GATE_BOARD_CLK_DELAY * 1 ps + 1 ps;
    ras_n_delayed   <=   transport ras_n  after GATE_BOARD_CLK_DELAY * 1 ps + 1 ps;
    cas_n_delayed   <=   transport cas_n  after GATE_BOARD_CLK_DELAY * 1 ps + 1 ps;
    we_n_delayed    <=   transport we_n   after GATE_BOARD_CLK_DELAY * 1 ps + 1 ps;
-- << START MEGAWIZARD INSERT DM PIN
    dm_delayed      <=   	dm;
 
-- << END MEGAWIZARD INSERT DM PIN


-- ---------------------------------------------------------------

    endit : process
    variable count          : integer := 0;
    variable ln : line;

    begin
        -- Stop simulation after test_complete or TINIT + 600000 clocks
        while ((count < (TINIT_CLOCKS+600000) ) and (test_complete /= '1')) loop
            count := count + 1;
            wait until clk_to_sdram(0)'event and clk_to_sdram(0) = '0';
        end loop;
        if (test_complete = '1') then
            if (pnf = '1') then
                write(ln, now);
                write(ln, string'("          --- SIMULATION PASSED --- "));
                writeline(output, ln);
                ASSERT false REPORT "--- SIMULATION PASSED ---" SEVERITY FAILURE ;
            else
                write(ln, now);
                write(ln, string'("          --- SIMULATION FAILED --- "));
                writeline(output, ln);
                ASSERT false REPORT "--- SIMULATION FAILED ---" SEVERITY FAILURE ;
            end if;
        else
                write(ln, now);
                write(ln, string'("          --- SIMULATION FAILED, DID NOT COMPLETE --- "));
                writeline(output, ln);
                ASSERT false REPORT "--- SIMULATION FAILED, DID NOT COMPLETE ---" SEVERITY FAILURE ;
        end if;
        wait;
    end process;

    process(clk_to_sdram(0), reset_n)
    begin
        if (reset_n = '0') then
            test_complete_count <= 0;
        elsif (clk_to_sdram(0)'event and clk_to_sdram(0) = '1') then
            if (test_complete = '1') then
            	test_complete_count <= test_complete_count + 1;
            end if;
        end if;

    end process;


    -- Watch the SDRAM command bus
    process (clk_to_ram)
        variable cmd_bus : std_logic_vector(2 downto 0);
        variable ln : line;
    begin
    if (clk_to_ram'event and clk_to_ram = '1') then
        if (TRUE) then

            cmd_bus := (ras_n_delayed, cas_n_delayed, we_n_delayed);
            case cmd_bus is
                when "000" =>       -- LMR command
                    write(ln, now);

                    if (ba_delayed = zero_one) then
                        write(ln, string'("          ELMR     settings = "));

                        if (a_delayed(0) = '0') then
                            write(ln, string'("DLL enable"));
                        end if;
                    else
       					write(ln, string'("          LMR      settings = "));

   	    				case a_delayed(1 downto 0) is
    	       	   			when "10" => write(ln, string'("BL = 4,"));
	    	   	   			when "11" => write(ln, string'("BL = 8,"));
	       	  				when others => write(ln, string'("BL = ??,"));
       					end case;

	      				case a_delayed(6 downto 4) is
	     			   		when "010" => write(ln, string'(" CL = 2.0,"));
	      		 			when "011" => write(ln, string'(" CL = 3.0,"));
	  	   	 		 		when "100" => write(ln, string'(" CL = 4.0,"));
	         				when "101" => write(ln, string'(" CL = 5.0,"));
	   		   				when "110" => write(ln, string'(" CL = 6.0,"));
	    			  		when others => write(ln, string'(" CL = ??,"));
       					end case;

      	 				if (a_delayed(8) = '1') then
	     			   		write(ln, string'(" DLL reset"));
     	  				end if;

                    end if;

                    writeline(output, ln);
                when "001" =>       -- ARF command
                    write(ln, now);
                    write(ln, string'("          ARF "));
                    writeline(output, ln);
                when "010" =>       -- PCH command
                    write(ln, now);
                    write(ln, string'("          PCH"));
                    if (a_delayed(10) = '1') then
                        write(ln, string'(" all banks "));
                    else
                        write(ln, string'(" bank "));
                        write(ln, auk_to_string(ba_delayed,16,gMEM_BANK_BITS));
                    end if;
                    writeline(output, ln);
                when "011" =>       -- ACT command
                    write(ln, now);
                    write(ln, string'("          ACT     row address "));
                    write(ln, auk_to_string(a_delayed,16,gMEM_ADDR_BITS));
                    write(ln, string'(                               " bank "));
                    write(ln, auk_to_string(ba_delayed,16,gMEM_BANK_BITS));
                    writeline(output, ln);
                when "100" =>       -- WR command
                    write(ln, now);
                    write(ln, string'("          WR to   col address "));
                    write(ln, auk_to_string(a_delayed,16,gMEM_ADDR_BITS));
                    write(ln, string'(                               " bank "));
                    write(ln, auk_to_string(ba_delayed,16,gMEM_BANK_BITS));
                    writeline(output, ln);
                when "101" =>       -- RD command
                    write(ln, now);
                    write(ln, string'("          RD from col address "));
                    write(ln, auk_to_string(a_delayed,16,gMEM_ADDR_BITS));
                    write(ln, string'(                               " bank "));
                    write(ln, auk_to_string(ba_delayed,16,gMEM_BANK_BITS));
                    writeline(output, ln);
                when "110" =>       -- BT command
                    write(ln, now);
                    write(ln, string'("          BT "));
                    writeline(output, ln);
                when "111" => null; -- NOP command
                when others => null;
            end case;
        else
        end if; -- if enabled
    end if;

    end process;


end rtl;





