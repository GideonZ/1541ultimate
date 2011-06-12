-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 16 bit (burst of 2), externally 4x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library unisim;
    use unisim.vcomponents.all;

library work;
    use work.mem_bus_pkg.all;

entity ext_mem_ctrl_v5_sdr is
generic (
    g_simulation       : boolean := false;
    A_Width            : integer := 15;
    SDRAM_WakeupTime   : integer := 40;     -- refresh periods
    SDRAM_Refr_period  : integer := 375 );
port (
    clock       : in  std_logic := '0';
    clk_shifted : in  std_logic := '0';
    reset       : in  std_logic := '0';

    inhibit     : in  std_logic := '0';
    is_idle     : out std_logic;

    req         : in  t_mem_burst_req;
    resp        : out t_mem_burst_resp;

	SDRAM_CLK	: out std_logic;
	SDRAM_CKE	: out std_logic;
    SDRAM_CSn   : out std_logic := '1';
	SDRAM_RASn  : out std_logic := '1';
	SDRAM_CASn  : out std_logic := '1';
	SDRAM_WEn   : out std_logic := '1';
    SDRAM_DQM   : out std_logic := '0';

    MEM_A       : out   std_logic_vector(A_Width-1 downto 0);
    MEM_D       : inout std_logic_vector(7 downto 0) := (others => 'Z'));
end ext_mem_ctrl_v5_sdr;    

-- ADDR: 25 24 23 ...
--        0  X  X ... SDRAM (32MB)

architecture Gideon of ext_mem_ctrl_v5_sdr is
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
    signal sdram_d_o      : std_logic_vector(MEM_D'range) := (others => '1');
    signal sdram_d_t      : std_logic_vector(3 downto 0) := "0000";
    signal delay          : integer range 0 to 15;
    signal inhibit_d      : std_logic;
    signal rwn_i	      : std_logic;
    signal mem_a_i        : std_logic_vector(MEM_A'range) := (others => '0');
    signal cs_n_i         : std_logic;
    signal col_addr       : std_logic_vector(9 downto 0) := (others => '0');
    signal refresh_cnt    : integer range 0 to SDRAM_Refr_period-1;
    signal do_refresh     : std_logic := '0';
    signal not_clock      : std_logic;
    signal rdata          : std_logic_vector(7 downto 0) := (others => '0');
    signal wdata          : std_logic_vector(7 downto 0) := (others => '0');
    signal refr_delay     : integer range 0 to 7;
    signal next_delay     : integer range 0 to 7;
    signal boot_cnt       : integer range 0 to SDRAM_WakeupTime-1 := SDRAM_WakeupTime-1;
    signal init_cnt       : integer range 0 to c_init_array'high;
    signal enable_sdram   : std_logic := '1';

    signal req_i          : std_logic;
    signal rack           : std_logic;
    signal dack           : std_logic_vector(3 downto 0) := "0000";
    signal dnext          : std_logic_vector(3 downto 0) := "0000";

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
    attribute iob of rdata     : signal is "true"; -- the general memctrl/rdata must be packed in IOB

    constant c_address_width    : integer := req.address'length;
    constant c_data_width       : integer := req.data'length;

    signal cmd_fifo_data_in     : std_logic_vector(c_address_width downto 0);
    signal cmd_fifo_data_out    : std_logic_vector(c_address_width downto 0);
    
    signal rwn_fifo             : std_logic;
    signal address_fifo         : std_logic_vector(c_address_width-1 downto 0);
    signal cmd_af               : std_logic;
    signal cmd_av               : std_logic;
    signal rdata_af             : std_logic;
    signal push_cmd             : std_logic;
begin
    addr_bank   <= address_fifo(3 downto 2);
    addr_row    <= address_fifo(24 downto 12);
    addr_column <= address_fifo(11 downto 4) & address_fifo(1 downto 0);

--    addr_row    <= address_fifo(24 downto 12);
--    addr_bank   <= address_fifo(11 downto 10);
--    addr_column <= address_fifo(9 downto 0);

    is_idle <= '1' when state = idle else '0';

    req_i      <= cmd_av;
    resp.ready <= not cmd_af;
    push_cmd   <= req.request and not cmd_af;
    
    -- resp.rack  <= rack;
    -- resp.dack  <= dack(0);
    -- resp.dnext <= dnext(0);
    -- resp.blast <= (dack(0) and not dack(1)) or (dnext(0) and not dnext(1));

    cmd_fifo_data_in <= req.read_writen & std_logic_vector(req.address);
    address_fifo     <= cmd_fifo_data_out(address_fifo'range);
    rwn_fifo         <= cmd_fifo_data_out(cmd_fifo_data_out'high);
    
    i_command_fifo: entity work.srl_fifo
    generic map (
        Width     => c_address_width + 1,
        Depth     => 15,
        Threshold => 3)
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => rack,
        PutElement  => push_cmd,
        FlushFifo   => '0',
        DataIn      => cmd_fifo_data_in,
        DataOut     => cmd_fifo_data_out,
        SpaceInFifo => open,
        AlmostFull  => cmd_af,
        DataInFifo  => cmd_av );

    i_read_fifo: entity work.srl_fifo
    generic map (
        Width     => c_data_width,
        Depth     => 15,
        Threshold => 6 )
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => req.data_pop,
        PutElement  => dack(0),
        FlushFifo   => '0',
        DataIn      => rdata,
        DataOut     => resp.data,
        SpaceInFifo => open,
        AlmostFull  => rdata_af,
        DataInFifo  => resp.rdata_av );

    i_write_fifo: entity work.SRL_fifo
    generic map (
        Width     => c_data_width,
        Depth     => 15,
        Threshold => 6 )
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => dnext(0),
        PutElement  => req.data_push,
        FlushFifo   => '0',
        DataIn      => req.data,
        DataOut     => wdata,
        SpaceInFifo => open,
        AlmostFull  => resp.wdata_full,
        DataInFifo  => open );
    
    process(clock)
        procedure send_refresh_cmd is
        begin
            if refr_delay = 0 then
                do_refresh <= '0';
                cs_n_i  <= '0';
                SDRAM_RASn <= '0';
                SDRAM_CASn <= '0';
                SDRAM_WEn  <= '1'; -- Auto Refresh
                refr_delay <= 3;
                next_delay <= 3;
            end if;
        end procedure;

        procedure accept_req is
        begin
			rwn_i     <= rwn_fifo;
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
            if rwn_fifo='0' then
                dnext  <= "1111";
                delay  <= 0;
            end if;
        end procedure;
    begin
            
        if rising_edge(clock) then
            inhibit_d  <= inhibit;
            cs_n_i     <= '1';
            SDRAM_CKE  <= enable_sdram;
            SDRAM_RASn  <= '1';
            SDRAM_CASn  <= '1';
            SDRAM_WEn   <= '1';
            sdram_d_o   <= wdata;
            
            if refr_delay /= 0 then
                refr_delay <= refr_delay - 1;
            end if;
            if next_delay /= 0 then
                next_delay <= next_delay - 1;
            end if;
            if delay /= 0 then
                delay <= delay - 1;
            end if;
            
            sdram_d_t <= '0' & sdram_d_t(3 downto 1);
            dack     <= '0' & dack(3 downto 1);
            dnext    <= '0' & dnext(3 downto 1);
            rdata    <= MEM_D;

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
                end if;

            when idle =>
                -- first cycle after inhibit goes 0, do not do refresh
                -- this enables putting cartridge images in sdram
				if do_refresh='1' and not (inhibit_d='1' or inhibit='1') then
				    send_refresh_cmd;
				elsif inhibit='0' then
    				if req_i='1' and next_delay = 0 then
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
                refr_delay <= 5;
                if (req_i='1' and addr_bank=last_bank) or req_i='0' then
                    next_delay <= 5;
                else
                    next_delay <= 2;
                end if;

                mem_a_i(10) <= '1'; -- auto precharge
                mem_a_i(9 downto 0) <= col_addr;

                if rwn_i='0' then
                    if delay=0 then
                        -- write with auto precharge
                        sdram_d_t <= "1111";
                        cs_n_i   <= '0';
                        SDRAM_RASn  <= '1';
                        SDRAM_CASn  <= '0';
                        SDRAM_WEn   <= '0';
                        delay   <= 2;
                        state   <= sd_wait;
                    end if;
                else
                    if delay = 0 then
                        if rdata_af='0' then
                            -- read with auto precharge
                            cs_n_i   <= '0';
                            SDRAM_RASn  <= '1';
                            SDRAM_CASn  <= '0';
                            SDRAM_WEn   <= '1';
                            delay   <= 2;
                            state   <= sd_wait;
                        end if;
                    end if;
                end if;

            when sd_wait =>
                if delay=1 then
                    state <= idle;
                end if;
                if delay=1 and rwn_i='1' then
                    dack <= "1111";
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
				sdram_d_t    <= (others => '0');
				delay        <= 0;
                do_refresh   <= '0';
                boot_cnt     <= SDRAM_WakeupTime-1;
                init_cnt     <= 0;
                enable_sdram <= '1';
            end if;
        end if;
    end process;
    
    process(state, do_refresh, inhibit, inhibit_d, req_i, next_delay)
    begin
        rack <= '0';
        case state is
            when idle =>
                -- first cycle after inhibit goes 0, do not do refresh
                -- this enables putting cartridge images in sdram
				if do_refresh='1' and not (inhibit_d='1' and inhibit='0') then
				    null;
				elsif inhibit='0' then
    				if req_i='1' and next_delay = 0 then
                        rack <= '1';
                    end if;
                end if;
            when others =>
                null;
        end case;
    end process;

    MEM_D       <= sdram_d_o after 7 ns when sdram_d_t(0)='1' else (others => 'Z') after 7 ns;
    MEM_A       <= mem_a_i;

    not_clock <= not clk_shifted;
    clkout: FDDRRSE
	port map (
		CE => '1',
		C0 => clk_shifted,
		C1 => not_clock,
		D0 => '0',
		D1 => enable_sdram,
		Q  => SDRAM_CLK,
		R  => '0',
		S  => '0' );

    SDRAM_CSn  <= cs_n_i;

end Gideon;
