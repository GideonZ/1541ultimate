library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library std;
use std.textio.all;

entity hf_noise_tb is
end;

architecture tb of hf_noise_tb is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal q               : signed(15 downto 0);
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;        

    i_hf: entity work.hf_noise 
    generic map (
        g_hp_filter     => true )
    port map (
        clock           => clock,
        reset           => reset,
        
        q               => q );

    process
        variable stat  : file_open_status;
        file myfile    : text;
        variable v_line : line;
        constant c_filename : string := "hello";
    begin
        -- open file
        file_open(stat, myfile, c_filename, write_mode);
        assert (stat = open_ok)
            report "Could not open file " & c_filename & " for writing."
            severity failure;

        wait until reset='0';
        wait until clock='1';
        for i in 1 to 1024*1024 loop
            wait until clock='1';
            write(v_line, integer'image(to_integer(q)));
            writeline(myfile, v_line);
        end loop;
        file_close(myfile);
        report "Done!" severity failure;
        wait;
    end process;

end tb;
