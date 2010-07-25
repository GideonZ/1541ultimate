-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 16 bit (burst of 2), externally 4x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.std_logic_arith.all;
    use ieee.std_logic_unsigned.all;

library unisim;
    use unisim.vcomponents.all;

library work;
    use work.mem_bus_pkg.all;

entity ext_mem_ctrl_v5 is
generic (
    g_simulation       : boolean := false;
    A_Width            : integer := 15;
    SDRAM_WakeupTime   : integer := 40;     -- refresh periods
    SDRAM_Refr_period  : integer := 375 );
port (
    clock       : in  std_logic := '0';
    clk_2x      : in  std_logic := '0';
    reset       : in  std_logic := '0';

    inhibit     : in  std_logic;
    is_idle     : out std_logic;

    req         : in  t_mem_burst_16_req;
    resp        : out t_mem_burst_16_resp;

	SDRAM_CLK	: out std_logic;
	SDRAM_CKE	: out std_logic;
    SDRAM_CSn   : out std_logic := '1';
	SDRAM_RASn  : out std_logic := '1';
	SDRAM_CASn  : out std_logic := '1';
	SDRAM_WEn   : out std_logic := '1';
    SDRAM_DQM   : out std_logic := '0';

    MEM_A       : out   std_logic_vector(A_Width-1 downto 0);
    MEM_D       : inout std_logic_vector(7 downto 0) := (others => 'Z'));
end ext_mem_ctrl_v5;    

-- ADDR: 25 24 23 ...
--        0  X  X ... SDRAM (32MB)

architecture Gideon of ext_mem_ctrl_v5 is
    type t_init is record
        addr    : std_logic_vector(15 downto 0);
        cmd     : std_logic_vector(2 downto 0);  -- we-cas-ras
    end record;
    type t_init_array is array(natural range <>) of t_init;
    constant c_init_array : t_init_array(0 to 7) := (
        ( X"0400", "010" ), -- auto precharge
        ( X"002A", "000" ), -- mode register, burstlen=4, writelen=4, CAS lat = 2, interleaved
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ) );

    type t_state is (boot, init, idle, sd_cas, sd_wait);
    signal state          : t_state;
    signal sram_d_o       : std_logic_vector(MEM_D'range) := (others => '1');
    signal sram_d_t       : std_logic_vector(1 downto 0) := "00";
    signal r_valid        : std_logic_vector(3 downto 0) := "0000";
    signal delay          : integer range 0 to 15;
    signal inhibit_d      : std_logic;
    signal rwn_i	      : std_logic;
    signal mem_a_i        : std_logic_vector(MEM_A'range) := (others => '0');
    signal cs_n_i         : std_logic;
    signal col_addr       : std_logic_vector(9 downto 0) := (others => '0');
    signal refresh_cnt    : integer range 0 to SDRAM_Refr_period-1;
    signal do_refresh     : std_logic := '0';
    signal not_clock      : std_logic;
    signal not_clock_2x   : std_logic;
    signal rdata_hi_d     : std_logic_vector(7 downto 0) := (others => '0');
    signal rdata_hi       : std_logic_vector(7 downto 0) := (others => '0');
    signal rdata_lo       : std_logic_vector(7 downto 0) := (others => '0');
    signal refr_delay     : integer range 0 to 3;
    signal boot_cnt       : integer range 0 to SDRAM_WakeupTime-1 := SDRAM_WakeupTime-1;
    signal init_cnt       : integer range 0 to c_init_array'high;
    signal enable_sdram   : std_logic := '1';

    signal req_i          : std_logic;
    signal dack           : std_logic;
    signal rack           : std_logic;
    signal dnext          : std_logic;

    signal last_bank      : std_logic_vector(1 downto 0) := "10";
    signal addr_bank      : std_logic_vector(1 downto 0);
    signal addr_row       : std_logic_vector(12 downto 0);
    signal addr_column    : std_logic_vector(9 downto 0);
    
--    attribute fsm_encoding : string;
--    attribute fsm_encoding of state : signal is "sequential";

--    attribute register_duplication : string;
--    attribute register_duplication of mem_a_i  : signal is "no";

    attribute iob : string;
    attribute iob of SDRAM_CKE : signal is "false";
--    attribute iob of rdata_i : signal is "true"; -- the general memctrl/rdata must be packed in IOB

begin
    addr_bank   <= std_logic_vector(req.address(3 downto 2));
    addr_row    <= std_logic_vector(req.address(24 downto 12));
    addr_column <= std_logic_vector(req.address(11 downto 4)) & std_logic_vector(req.address(1 downto 0));

    is_idle <= '1' when state = idle else '0';

    req_i <= req.request;

    resp.data  <= rdata_hi_d & rdata_lo;
    resp.rack  <= rack;
    resp.dack  <= dack;
    resp.dnext <= dnext;

    process(clock)
        procedure send_refresh_cmd is
        begin
            do_refresh <= '0';
            cs_n_i  <= '0';
            SDRAM_RASn <= '0';
            SDRAM_CASn <= '0';
            SDRAM_WEn  <= '1'; -- Auto Refresh
            refr_delay <= 3; 
        end procedure;

        procedure accept_req is
        begin
			rwn_i     <= req.read_writen;
            last_bank <= addr_bank;
            
            mem_a_i(12 downto 0)  <= addr_row;
            mem_a_i(14 downto 13) <= addr_bank;
            col_addr              <= addr_column;
            cs_n_i     <= '0';
            SDRAM_RASn <= '0';
            SDRAM_CASn <= '1';
            SDRAM_WEn  <= '1'; -- Command = ACTIVE
            delay      <= 0;
            state  <= sd_cas;                            
            dnext  <= '1';   -- if we set delay to a value not equal to zero, we should not
                             -- set the dnext here.
        end procedure;
    begin
            
        if rising_edge(clock) then
			dack       <= '0';
			dnext      <= '0';
            inhibit_d  <= inhibit;
            rdata_hi_d <= rdata_hi;
            cs_n_i     <= '1';
            SDRAM_CKE  <= enable_sdram;
            
            if refr_delay /= 0 then
                refr_delay <= refr_delay - 1;
            end if;

            sram_d_t <= '0' & sram_d_t(1);
            r_valid  <= '0' & r_valid(3 downto 1);

            case state is
            when boot =>
                enable_sdram <= '1';
                if refresh_cnt = 0 then
                    boot_cnt <= boot_cnt - 1;
                    if boot_cnt = 1 then
                        state <= init;
                    end if;
                elsif g_simulation then
                    state <= idle;
                end if;

            when init =>
                mem_a_i    <= c_init_array(init_cnt).addr(mem_a_i'range);
                SDRAM_RASn <= c_init_array(init_cnt).cmd(0);
                SDRAM_CASn <= c_init_array(init_cnt).cmd(1);
                SDRAM_WEn  <= c_init_array(init_cnt).cmd(2);
                if delay = 0 then
                    delay <= 7;
                    cs_n_i <= '0';
                    if init_cnt = c_init_array'high then
                        state <= idle;
                    else
                        init_cnt <= init_cnt + 1;
                    end if;
                else
                    delay <= delay - 1;
                end if;

            when idle =>
                -- first cycle after inhibit goes 0, do not do refresh
                -- this enables putting cartridge images in sdram
				if do_refresh='1' and not (inhibit_d='1' or inhibit='1') then
				    send_refresh_cmd;
				elsif inhibit='0' then
    				if req_i='1' and refr_delay = 0 then
                        accept_req;
                    end if;
                end if;
					
            
			when sd_cas =>
                -- we always perform auto precharge.
                -- If the next access is to ANOTHER bank, then
                -- we do not have to wait AFTER issuing this CAS.
                -- the delay after the CAS, causes the next RAS to
                -- be further away in time. If there is NO access
                -- pending, then we assume the same bank, and introduce
                -- the delay.
                if (req_i='1' and addr_bank=last_bank) or req_i='0' then
                    refr_delay <= 2;
                end if;

                mem_a_i(10) <= '1'; -- auto precharge
                mem_a_i(9 downto 0) <= col_addr;

                if delay <= 1 then
                    dnext <= '1';
                end if;

                if delay = 0 then
                    if rwn_i='0' then
                        sram_d_t <= "11";
                    else
                        r_valid(3 downto 2) <= "11";
                    end if;

                    -- read or write with auto precharge
                    cs_n_i   <= '0';
                    SDRAM_RASn  <= '1';
                    SDRAM_CASn  <= '0';
                    SDRAM_WEn   <= rwn_i;
                    if rwn_i='0' then -- write
                        delay   <= 2;
                    else
                        delay   <= 1;
                    end if;
                    state   <= idle;
                else
                    delay <= delay - 1;
                end if;
                
            when others =>
                null;

            end case;

            if refresh_cnt = SDRAM_Refr_period-1 then
                do_refresh  <= '1';
                refresh_cnt <= 0;
            else
                refresh_cnt <= refresh_cnt + 1;
            end if;

            if reset='1' then
                state        <= boot;
--				sram_d_t     <= (others => '0');
				delay        <= 0;
                do_refresh   <= '0';
                boot_cnt     <= SDRAM_WakeupTime-1;
                init_cnt     <= 0;
                enable_sdram <= '1';
            end if;
        end if;
    end process;
    
    process(state, do_refresh, inhibit, inhibit_d, req_i, refr_delay)
    begin
        rack <= '0';
        case state is
            when idle =>
                -- first cycle after inhibit goes 0, do not do refresh
                -- this enables putting cartridge images in sdram
				if do_refresh='1' and not (inhibit_d='1' and inhibit='0') then
				    null;
				elsif inhibit='0' then
    				if req_i='1' and refr_delay = 0 then
                        rack <= '1';
                    end if;
                end if;
            when others =>
                null;
        end case;
    end process;

    MEM_D       <= sram_d_o when sram_d_t(0)='1' else (others => 'Z');
    MEM_A       <= mem_a_i;

    not_clock_2x <= not clk_2x;
    not_clock    <= not clock;
    
    clkout: FDDRRSE
	port map (
		CE => '1',
		C0 => clk_2x,
		C1 => not_clock_2x,
		D0 => '0',
		D1 => enable_sdram,
		Q  => SDRAM_CLK,
		R  => '0',
		S  => '0' );

    select_out: FDDRRSE
	port map (
		CE => '1',
		C0 => clock,
		C1 => not_clock,
		D0 => '1',
		D1 => cs_n_i,
		Q  => SDRAM_CSn,
		R  => '0',
		S  => '0' );

    r_data: for i in 0 to 7 generate
        i_in: IDDR2
    	generic map (
    		DDR_ALIGNMENT => "NONE",
    		SRTYPE        => "SYNC"	)
    	port map (
    		Q0 => rdata_lo(i),
    		Q1 => rdata_hi(i),
    		C0 => clock,
    		C1 => not_clock,
    		CE => '1',
    		D =>  MEM_D(i),
    		R =>  reset,
    		S =>  '0');

        i_out: ODDR2
    	generic map (
    		DDR_ALIGNMENT => "NONE",
    		SRTYPE        => "SYNC" )
    	port map (
    		Q  => sram_d_o(i),
    		C0 => clock,
    		C1 => not_clock,
    		CE => '1',
    		D0 => req.data(8+i),
    		D1 => req.data(i),
    		R  => reset,
    		S  => '0' );

    end generate;

end Gideon;
