library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library unisim;
use unisim.vcomponents.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity cpu_wrapper_zpu is
generic (
    g_mem_tag         : std_logic_vector(7 downto 0) := X"20";
    g_internal_prg    : boolean := true;
    g_boot_rom        : boolean := false;
    g_simulation      : boolean := false );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    irq_i       : in  std_logic;
    break_o     : out std_logic;
    error		: out std_logic;
	
    -- memory interface
    mem_req     : out t_mem_req;
    mem_resp    : in  t_mem_resp;
    
    io_req      : out t_io_req;
    io_resp     : in  t_io_resp );

end cpu_wrapper_zpu;


architecture logical of cpu_wrapper_zpu is
    signal cpu_address  : std_logic_vector(26 downto 0);
    signal cpu_size     : std_logic_vector(1 downto 0);
    signal cpu_rdata    : std_logic_vector(7 downto 0);
    signal cpu_wdata    : std_logic_vector(7 downto 0);
    signal cpu_write    : std_logic;
    signal cpu_instr    : std_logic;
    signal cpu_req      : std_logic;
    signal cpu_rack     : std_logic;
    signal cpu_dack     : std_logic;

    signal ram_rack     : std_logic := '0';
    signal ram_dack     : std_logic := '0';
    signal ram_claimed  : std_logic := '0';
    signal ram_rdata    : std_logic_vector(7 downto 0) := X"FF";
    
    signal mem_rack     : std_logic := '0';
    signal mem_dack     : std_logic := '0';

	signal break_o_i	: std_logic;
	signal reset_cpu	: std_logic;
	
    type t_state is (idle, busy);
    signal state        : t_state;    

	type t_crash_state is (all_ok, flash_on, flash_off);
	signal crash_state 	: t_crash_state := all_ok;
	signal delay		: integer range 0 to 8388607 := 0;
begin
	break_o <= break_o_i;

    core: entity work.zpu
    generic map (
        g_addr_size  => cpu_address'length,
        g_stack_size => 12,
        g_prog_size  => 20,  -- Program size
        g_dont_care  => '-') -- Value used to fill the unused bits, can be '-' or '0'
    port map (
        clock        => clock,
        reset        => reset_cpu,
        interrupt_i  => irq_i,
        break_o      => break_o_i,

        mem_address  => cpu_address,
        mem_instr    => cpu_instr,
        mem_size     => cpu_size,
        mem_req      => cpu_req,
        mem_write    => cpu_write,
        mem_rack     => cpu_rack,
        mem_dack     => cpu_dack,
        mem_wdata    => cpu_wdata,
        mem_rdata    => cpu_rdata );


    r_int_ram: if g_internal_prg generate
        r_boot: if g_boot_rom generate
            i_zpuram: entity work.mem32k
            generic map (
                simulation => g_simulation )
            port map (
                clock   => clock,
                reset   => reset,
                address => cpu_address,
                request => cpu_req,
                mwrite  => cpu_write,
                wdata   => cpu_wdata,
                rdata   => ram_rdata,
                rack    => ram_rack,
                dack    => ram_dack,
                claimed => ram_claimed );
        end generate;
        r_noboot: if not g_boot_rom generate
            i_zpuram: entity work.mem4k
            generic map (
                simulation => g_simulation )
            port map (
                clock   => clock,
                reset   => reset,
                address => cpu_address,
                request => cpu_req,
                mwrite  => cpu_write,
                wdata   => cpu_wdata,
                rdata   => ram_rdata,
                rack    => ram_rack,
                dack    => ram_dack,
                claimed => ram_claimed );
        end generate;
    end generate;

    cpu_rdata <= io_resp.data when io_resp.ack='1'
            else mem_resp.data when mem_dack='1'
            else ram_rdata;

    cpu_rack  <= io_resp.ack or mem_rack or ram_rack;
    cpu_dack  <= (io_resp.ack and not cpu_write) or mem_dack or ram_dack;

    mem_req.request     <= '1' when cpu_req='1' and cpu_address(26)='0' and ram_claimed='0'
                      else '0'; 
    mem_req.tag         <= g_mem_tag;
    mem_req.address     <= unsigned(cpu_address(25 downto 0));
    mem_req.read_writen <= not cpu_write;
    mem_req.data        <= cpu_wdata;
    
    mem_rack <= '1' when mem_resp.rack_tag = g_mem_tag else '0';
    mem_dack <= '1' when mem_resp.dack_tag = g_mem_tag else '0';

    io_req.address(19 downto 2) <= unsigned(cpu_address(19 downto 2));
	with cpu_address(24) select
    	io_req.address( 1 downto 0) <=
    		unsigned(cpu_address(1 downto 0) xor cpu_size) when '0',
    		unsigned(cpu_address(1 downto 0)) when others;
	    
    io_req.data <= cpu_wdata;
    
    p_io: process(clock)
    begin
        if rising_edge(clock) then
            io_req.read  <= '0';
            io_req.write <= '0';
            case state is
            when idle =>
                if cpu_req='1' and cpu_address(26)='1' then
                    io_req.read  <= not cpu_write;
                    io_req.write <= cpu_write;
                    state <= busy;
                end if;
            when busy =>
                if io_resp.ack='1' then
                    state <= idle;
                end if;
            when others =>
                null;
            end case;

            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;

	p_crash: process(clock)
	begin
		if rising_edge(clock) then
			case crash_state is
			when all_ok =>
				reset_cpu <= '0';
				error <= '0';
				delay <= 6_250_000;
				if break_o_i='1' then
					reset_cpu <= '1';
					crash_state <= flash_on;
				end if;
			
			when flash_on =>
				reset_cpu <= '1';
				error <= '1';
				if delay = 0 then
					crash_state <= flash_off;
					delay <= 6_250_000;
				else
					delay <= delay - 1;
				end if;
			
			when flash_off =>
				reset_cpu <= '1';
				error <= '0';
				if delay = 0 then
					crash_state <= flash_on;
					delay <= 6_250_000;
				else
					delay <= delay - 1;
				end if;

			when others =>
				crash_state <= flash_on;
				
			end case;
			
			if reset='1' then
				error <= '0';
				reset_cpu <= '1';
				crash_state <= all_ok;
			end if;
		end if;
	end process;
end logical;
