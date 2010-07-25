library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_signed.all;

entity sigma_delta_dac is
generic (
    width   : positive := 12 );
port (
    clk     : in  std_logic;
    reset   : in  std_logic;
    
    dac_in  : in  std_logic_vector(width-1 downto 0);

    dac_out : out std_logic );

end sigma_delta_dac;

architecture gideon of sigma_delta_dac is
    signal delta_sum   : std_logic_vector(width+1 downto 0);
    signal sigma_reg   : std_logic_vector(width+1 downto 0);
    constant zeros     : std_logic_vector(31 downto 0) := (others => '0');    
begin
    delta_sum <= ("00" & dac_in) + (sigma_reg(width+1) & sigma_reg(width+1) & zeros(width-1 downto 0));

    process(clk)
    begin
        if rising_edge(clk) then
            sigma_reg <= sigma_reg + delta_sum;            
            dac_out   <= sigma_reg(width+1);
            
            if reset='1' then
                dac_out   <= '0';
                sigma_reg <= (others => '0');
                sigma_reg(width+1) <= '1';
            end if;
        end if;
    end process;

end gideon;