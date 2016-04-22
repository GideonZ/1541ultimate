library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;


--
entity ddr2_ex_lfsr8 is

    generic
    (
        seed                 : integer := 32        -- starting seed
    );
    port
    (
        -- globals
        clk                 : in   std_logic;
        reset_n             : in   std_logic;

        -- local interface, read and write data
        enable               : in   std_logic;
        pause                : in   std_logic;
        load                 : in   std_logic;

        data                 : out  std_logic_vector(8 - 1 downto 0);
        ldata                : in   std_logic_vector(8 - 1 downto 0)
    );
--
end ddr2_ex_lfsr8;

--
architecture rtl of ddr2_ex_lfsr8 is
signal  lfsr_data  : std_logic_vector(8 - 1 downto 0);

begin

    data <= lfsr_data;

    process(clk, reset_n)
    begin
        if reset_n = '0' then
            -- Reset - asynchronously reset to seed value
            lfsr_data <= conv_std_logic_vector(seed, 8);

        elsif rising_edge(clk) then
            -- Registered mode - synchronous propagation of signals

			if (enable = '0') then
				lfsr_data <= conv_std_logic_vector(seed, 8);
			else
				if (load = '1') then
					lfsr_data <= ldata;
				else
					if (pause = '0') then
			   		    lfsr_data(0) <= lfsr_data(7);
				  		lfsr_data(1) <= lfsr_data(0);
				   		lfsr_data(2) <= lfsr_data(1) xor lfsr_data(7);
				   		lfsr_data(3) <= lfsr_data(2) xor lfsr_data(7);
			 	   		lfsr_data(4) <= lfsr_data(3) xor lfsr_data(7);
			  	  		lfsr_data(5) <= lfsr_data(4);
			   	 		lfsr_data(6) <= lfsr_data(5);
			    		lfsr_data(7) <= lfsr_data(6);
					end if;
				end if;
			end if;
        end if;
    end process;

end rtl;

