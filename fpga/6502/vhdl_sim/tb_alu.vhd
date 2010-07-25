library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library work;
use work.file_io_pkg.all;

library std;
use std.textio.all;

entity tb_alu is
end tb_alu;

architecture tb of tb_alu is
    signal c_in            : std_logic := '0';
    signal data_a          : std_logic_vector(7 downto 0) := X"00";
    signal data_b          : std_logic_vector(7 downto 0) := X"00";
    signal n_out           : std_logic;
    signal v_out           : std_logic;
    signal z_out           : std_logic;
    signal c_out           : std_logic;
    signal data_out        : std_logic_vector(7 downto 0);
    signal p_out           : std_logic_vector(7 downto 0);
    signal operation       : std_logic_vector(2 downto 0) := "011"; -- ADC

    signal p_reference     : std_logic_vector(7 downto 0);
    signal n_ref           : std_logic;
    signal v_ref           : std_logic;
    signal z_ref           : std_logic;
    signal c_ref           : std_logic;
begin
    p_out <= n_out & v_out & "111" & '0' & z_out & c_out;
    
    ref_sum: process -- taken from real 6510
    begin
        wait for 1 ps;
        if operation(2)='0' then -- adc
            p_reference <= X"3A";   wait for 1 us;
            p_reference <= X"38";   wait for 121 us;
            p_reference <= X"F8";   wait for 6 us;
            p_reference <= X"B8";   wait for 26 us;
            p_reference <= X"B9";   wait for 96 us;
            p_reference <= X"39";   wait for 6 us;
            
            -- 01
            p_reference <= X"38";   wait for 121 us;
            p_reference <= X"F8";   wait for 7 us;
            p_reference <= X"B8";   wait for 25 us;
            p_reference <= X"B9";   wait for 96 us;
            p_reference <= X"39";   wait for 6 us;
            p_reference <= X"3B";   wait for 1 us;
        else
            p_reference <= X"3B";   wait for 1 us;
            p_reference <= X"B8";   wait for 127 us;
            p_reference <= X"F8";   wait for 1 us;
            p_reference <= X"38";   wait for 127 us;
            
            -- 01
            p_reference <= X"39";   wait for 1 us;
            p_reference <= X"3B";   wait for 1 us;
            p_reference <= X"B8";   wait for 126 us;
            p_reference <= X"F8";   wait for 2 us;
            p_reference <= X"38";   wait for 126 us;

        end if;
        p_reference <= (others => 'U');
        wait;        
    end process;
    n_ref <= p_reference(7);        
    v_ref <= p_reference(6);        
    z_ref <= p_reference(1);        
    c_ref <= p_reference(0);        
         
    test: process
        variable L    : line;
    begin
        for i in 0 to 3 loop
--            data_a <= conv_std_logic_vector((i mod 10) + (i/10)*16,8);
            data_a <= conv_std_logic_vector(i,8);
            for j in 0 to 255 loop
                data_b <= conv_std_logic_vector(j,8);
                c_in   <= operation(2);
                wait for 500 ns;
                -- check flags
                assert c_out = c_ref or c_ref = 'U' report "Error in C-flag!" severity error;
                assert z_out = z_ref or z_ref = 'U' report "Error in Z-flag!" severity error;
                assert v_out = v_ref or v_ref = 'U' report "Error in V-flag!" severity error;
                assert n_out = n_ref or n_ref = 'U' report "Error in N-flag!" severity error;
                wait for 500 ns;
                

--                write(L, VecToHex(data_out, 2));
                
--                write(L, VecToHex(p_out, 2));
--                write(L, ',');
--                c_in   <= '1';
--                wait for 1 us;
            end loop;
--            writeline(output, L);
        end loop;
        wait;
    end process;

    mut: entity work.alu
    generic map (true)
    port map (
        operation       => operation,
        enable          => '1',
            
        n_in            => 'U',
        v_in            => 'U',
        z_in            => 'U',
        c_in            => c_in,
        d_in            => '1',
        
        data_a          => data_a,
        data_b          => data_b,
        
        n_out           => n_out,
        v_out           => v_out,
        z_out           => z_out,
        c_out           => c_out,
        
        data_out        => data_out );
        
end tb;