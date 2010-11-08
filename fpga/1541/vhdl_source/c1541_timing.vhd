library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;


entity c1541_timing is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    use_c64_reset   : in  std_logic;
    c64_reset_n     : in  std_logic;
    iec_reset_n     : in  std_logic;
    iec_reset_o     : out std_logic;

    drive_stop      : in  std_logic;
                    
    drv_clock_en    : out std_logic;   -- 1/12.5 (4 MHz)
    cpu_clock_en    : out std_logic ); -- 1/50   (1 MHz)
    
end c1541_timing;

architecture Gideon of c1541_timing is
    signal div_cnt          : unsigned(3 downto 0) := "0000";
    signal pre_cnt          : unsigned(1 downto 0) := "00";
	signal cpu_clock_en_i	: std_logic := '0';
    signal toggle           : std_logic := '0';
    signal iec_reset_sh     : std_logic_vector(0 to 2) := "000";
    signal c64_reset_sh     : std_logic_vector(0 to 2) := "000";
begin
    process(clock)
    begin
        if rising_edge(clock) then
			drv_clock_en   <= '0';
			cpu_clock_en_i <= '0';
			
            if drive_stop='0' then
    			if (div_cnt = X"B" and toggle='0') or
    			   (div_cnt = X"C" and toggle='1') then
    				div_cnt <= X"0";
    				drv_clock_en <= '1';
    
                    toggle <= not toggle;
    
    	            pre_cnt <= pre_cnt + 1;
    	
    	            if pre_cnt = "11" then
    	                cpu_clock_en_i <= '1';
    	            end if;
    			else
    				div_cnt <= div_cnt + 1;
    			end if;
            end if;

            if cpu_clock_en_i = '1' then
                iec_reset_sh(0) <= not iec_reset_n;
                iec_reset_sh(1 to 2) <= iec_reset_sh(0 to 1);

                c64_reset_sh(0) <= use_c64_reset and not c64_reset_n;
                c64_reset_sh(1 to 2) <= c64_reset_sh(0 to 1);
            end if;                    

            if reset='1' then
                toggle      <= '0';
                pre_cnt     <= (others => '0');
                div_cnt     <= (others => '0');
            end if;
        end if;
    end process;

	cpu_clock_en <= cpu_clock_en_i;
    iec_reset_o  <= '1' when (iec_reset_sh="111") or (c64_reset_sh="111") else '0';
end Gideon;
