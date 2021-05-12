library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;


entity c1541_timing is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    two_MHz_mode    : in  std_logic := '0';
    tick_4MHz       : in  std_logic;
    mem_busy        : in  std_logic := '0';
    
    use_c64_reset   : in  std_logic;
    c64_reset_n     : in  std_logic;
    iec_reset_n     : in  std_logic;
    iec_reset_o     : out std_logic;

    drive_stop      : in  std_logic;
                    
    cia_rising      : out std_logic;
    cpu_clock_en    : out std_logic ); 
    
end c1541_timing;

architecture Gideon of c1541_timing is
    signal pre_cnt          : unsigned(1 downto 0) := "00";
	signal cpu_clock_en_i	: std_logic := '0';
    signal iec_reset_sh     : std_logic_vector(0 to 2) := "000";
    signal c64_reset_sh     : std_logic_vector(0 to 2) := "000";
    signal behind           : unsigned(3 downto 0);
begin
    process(clock)
    begin
        if rising_edge(clock) then
			cpu_clock_en_i <= '0';
            cia_rising     <= '0';			
            if drive_stop='0' then
                if tick_4MHz = '1' then
                    case pre_cnt is
                    when "00" =>
                        pre_cnt <= "01";
                    when "01" =>
                        cia_rising <= '1';
                        if behind = 0 then
                            if two_MHz_mode = '1' then
                                pre_cnt <= "11";
                            else
                                pre_cnt <= "10";
                            end if;
                        else
                            behind <= behind - 1;
                            pre_cnt <= "11";
                        end if;
                    when "10" =>
                        pre_cnt <= "11";
                    when others => -- 11
                        if mem_busy = '0' then
                            cpu_clock_en_i <= '1';
                            if two_MHz_mode = '1' then
                                pre_cnt <= "01";
                            else
                                pre_cnt <= "00";
                            end if;
                        elsif signed(behind) /= -1 then
                            behind <= behind + 1;
                        end if;                        
                    end case;
    			end if;
            end if;

            if cpu_clock_en_i = '1' then
                iec_reset_sh(0) <= not iec_reset_n;
                iec_reset_sh(1 to 2) <= iec_reset_sh(0 to 1);

                c64_reset_sh(0) <= use_c64_reset and not c64_reset_n;
                c64_reset_sh(1 to 2) <= c64_reset_sh(0 to 1);
            end if;                    

            if reset='1' then
                pre_cnt     <= (others => '0');
                behind      <= (others => '0');
            end if;
        end if;
    end process;

	cpu_clock_en <= cpu_clock_en_i;
    iec_reset_o  <= '1' when (iec_reset_sh="111") or (c64_reset_sh="111") else '0';
end Gideon;
