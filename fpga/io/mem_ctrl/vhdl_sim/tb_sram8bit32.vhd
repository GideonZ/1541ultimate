library ieee;
    use ieee.std_logic_1164.all;
    use ieee.std_logic_arith.all;
    use ieee.std_logic_unsigned.all;

entity tb_sram_8bit32 is
end tb_sram_8bit32;

architecture tb of tb_sram_8bit32 is
    signal clock       : std_logic := '0';
    signal reset       : std_logic := '0';
    signal req         : std_logic := '0';
    signal readwriten  : std_logic := '1';
    signal address     : std_logic_vector(19 downto 0) := (others => '0');
    signal rack        : std_logic;
	signal dack		   : std_logic;
    signal wdata       : std_logic_vector(7 downto 0) := X"12";
    signal rdata       : std_logic_vector(7 downto 0);
    signal SRAM_A      : std_logic_vector(17 downto 0);
    signal SRAM_OEn    : std_logic;
    signal SRAM_WEn    : std_logic;
    signal SRAM_CSn    : std_logic;
    signal SRAM_D      : std_logic_vector(31 downto 0) := (others => 'Z');
    signal SRAM_BEn    : std_logic_vector(3 downto 0);

begin
	clock <= not clock after 10 ns;
	reset <= '1', '0' after 100 ns;

	mut: entity work.sram_8bit32
	generic map (
		SRAM_WR_ASU		   => 0,
		SRAM_WR_Pulse      => 1,
		SRAM_WR_Hold       => 1,
	
		SRAM_RD_ASU		   => 0,
		SRAM_RD_Pulse      => 1,
		SRAM_RD_Hold	   => 1 ) -- recovery time (bus turnaround)
		
	port map (
	    clock       => clock,
	    reset       => reset,
	
	    req         => req,
	    readwriten  => readwriten,
	    address     => address,
	    rack        => rack,
		dack		=> dack,
		
	    wdata       => wdata,
	    rdata       => rdata,
	
		--
	    SRAM_A      => SRAM_A,
	    SRAM_OEn    => SRAM_OEn,
	    SRAM_WEn    => SRAM_WEn,
	    SRAM_CSn    => SRAM_CSn,
	    SRAM_D      => SRAM_D,
	    SRAM_BEn    => SRAM_BEn );

	SRAM_D <= "LLLLLLLHLLLLLLHLLLLLLLHHLLLLLHLL"; -- 01020304
	
	test: process
		procedure do_access(a : std_logic_vector; rw : std_logic; d : std_logic_vector) is
		begin
			req        <= '1';
			readwriten <= rw;
			address    <= a;
			wdata      <= d;
			wait until clock='1';
			while rack='0' loop
				wait until clock='1';
			end loop;
			req <= '0';
--			while dack='0' loop
--				wait until clock='1';
--			end loop;
		end do_access;
	begin
		wait until reset='0';
		wait until clock='1';

		do_access(X"01111", '1', X"00");
		do_access(X"02233", '1', X"00");
		do_access(X"04440", '0', X"55");
		do_access(X"04441", '0', X"56");
		do_access(X"04442", '0', X"57");
		do_access(X"04443", '0', X"58");
		do_access(X"05678", '0', X"12");
		do_access(X"09999", '1', X"00");
		
		wait;
	end process;

end tb;
