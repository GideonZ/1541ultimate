-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Asynchronous SRAM Controller
-------------------------------------------------------------------------------
-- File       : simple_sram.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single access sram controller.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.std_logic_arith.all;
    use ieee.std_logic_unsigned.all;

entity simple_sram is
generic (
	tag_width		   : integer := 2;
	
    SRAM_Byte_Lanes    : integer := 4;
	SRAM_Data_Width	   : integer := 32;

	SRAM_WR_ASU		   : integer := 0;
	SRAM_WR_Pulse      : integer := 1; -- 2 cycles in total
	SRAM_WR_Hold       : integer := 1;

	SRAM_RD_ASU		   : integer := 0;
	SRAM_RD_Pulse      : integer := 1;
	SRAM_RD_Hold	   : integer := 1; -- recovery time (bus turnaround)
	
    SRAM_A_Width       : integer := 18 );
port (
    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';

    req         : in  std_logic;
    req_tag		: in  std_logic_vector(1 to tag_width) := (others => '0');
    readwriten  : in  std_logic;
    address     : in  std_logic_vector(SRAM_A_Width-1 downto 0);
    rack        : out std_logic;
	dack		: out std_logic;
    rack_tag    : out std_logic_vector(1 to tag_width);
	dack_tag	: out std_logic_vector(1 to tag_width);
	
    wdata       : in  std_logic_vector(SRAM_Data_Width-1 downto 0);
	wdata_mask	: in  std_logic_vector(SRAM_Byte_Lanes-1 downto 0) := (others => '0');
    rdata       : out std_logic_vector(SRAM_Data_Width-1 downto 0);

	--
    SRAM_A      : out   std_logic_vector(SRAM_A_Width-1 downto 0);
    SRAM_OEn    : out   std_logic;
    SRAM_WEn    : out   std_logic;
    SRAM_CSn    : out   std_logic;
    SRAM_D      : inout std_logic_vector(SRAM_Data_Width-1 downto 0) := (others => 'Z');
    SRAM_BEn    : out   std_logic_vector(SRAM_Byte_Lanes-1 downto 0) );
end simple_sram;    

architecture Gideon of simple_sram is
    type t_state is (idle, setup, pulse, hold);
    signal state     : t_state;
    signal sram_d_o  : std_logic_vector(SRAM_D'range);
    signal sram_d_t  : std_logic;
    signal delay     : integer range 0 to 7;
    signal rwn_i	 : std_logic;
    signal tag		 : std_logic_vector(1 to tag_width);
begin
	assert SRAM_WR_Hold > 0  report "Write hold time should be greater than 0." severity failure;
	assert SRAM_RD_Hold > 0  report "Read hold time should be greater than 0 for bus turnaround." severity failure;
	assert SRAM_WR_Pulse > 0 report "Write pulse time should be greater than 0." severity failure;
	assert SRAM_RD_Pulse > 0 report "Read pulse time should be greater than 0." severity failure;

    process(clock)
    begin
        if rising_edge(clock) then
            rack  <= '0';
			dack  <= '0';
			rack_tag <= (others => '0');
			dack_tag <= (others => '0');
			
            rdata <= SRAM_D;  -- clock in
            
            case state is
            when idle =>
				if req='1' then
					rack     <= '1';
					rack_tag <= req_tag;
					tag		 <= req_tag;
					rwn_i    <= readwriten;
					SRAM_A   <= address;
					sram_d_t <= not readwriten;
					sram_d_o <= wdata;
										
					if readwriten='0' then -- write
						SRAM_BEn <= not wdata_mask;
						if SRAM_WR_ASU=0 then
							SRAM_WEn <= '0';
							state <= pulse;
							delay <= SRAM_WR_Pulse;
						else
							delay <= SRAM_WR_ASU;
							state <= setup;
						end if;
					else -- read
						SRAM_BEn <= (others => '0');
						if SRAM_RD_ASU=0 then
							SRAM_OEn <= '0';
							state <= pulse;
							delay <= SRAM_RD_Pulse;
						else
							delay <= SRAM_RD_ASU;
							state <= setup;
						end if;
					end if;
				end if;
					
			when setup =>
				if delay = 1 then
					if rwn_i='0' then
						delay <= SRAM_WR_Pulse;
						SRAM_WEn <= '0';
						state <= pulse;
					else
						delay <= SRAM_RD_Pulse;
						SRAM_OEn <= '0';
						state <= pulse;
					end if;
				else
					delay <= delay - 1;
				end if;

			when pulse =>
				if delay = 1 then
					SRAM_OEn <= '1';
					SRAM_WEn <= '1';
					dack     <= '1';
					dack_tag <= tag;
					if rwn_i='0' and SRAM_WR_Hold > 1 then
						delay <= SRAM_WR_Hold - 1;
						state <= hold;
				    elsif rwn_i='1' and SRAM_RD_Hold > 1 then
						state <= hold;				    	
				    	delay <= SRAM_RD_Hold - 1;
					else
						sram_d_t <= '0';
						state    <= idle;
				    end if;
				else
					delay <= delay - 1;
			    end if;
         
			when hold =>
				if delay = 1 then
					sram_d_t <= '0';
					state <= idle;
				else
					delay <= delay - 1;
			    end if;

            when others =>
                null;

            end case;
            if reset='1' then
                SRAM_BEn  <= (others => '1');
                sram_d_o  <= (others => '1');
				sram_d_t  <= '0';
                SRAM_OEn  <= '1';
				SRAM_WEn  <= '1';
				delay     <= 0;
				tag		  <= (others => '0');
            end if;
        end if;
    end process;
    
    SRAM_CSn    <= '0';
    SRAM_D      <= sram_d_o when sram_d_t='1' else (others => 'Z');
   
end Gideon;
