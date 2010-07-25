library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library std;
use std.textio.all;

library work;
use work.pkg_6502_opcodes.all;
use work.pkg_6502_decode.all;
use work.File_IO_pkg.all;

entity tb_data_oper is
end tb_data_oper;

architecture tb of tb_data_oper is

    signal inst      : std_logic_vector(7 downto 0);
    signal n_in      : std_logic := 'Z';
    signal v_in      : std_logic;
    signal z_in      : std_logic;
    signal c_in      : std_logic := 'U';
    signal d_in      : std_logic := 'U';
    signal i_in      : std_logic;
    signal data_in   : std_logic_vector(7 downto 0) := X"55";
    signal a_reg     : std_logic_vector(7 downto 0) := X"33";
    signal x_reg     : std_logic_vector(7 downto 0) := X"AB";
    signal y_reg     : std_logic_vector(7 downto 0) := X"CD";
    signal s_reg     : std_logic_vector(7 downto 0) := X"EF";
    signal alu_out   : std_logic_vector(7 downto 0);
    signal mem_out   : std_logic_vector(7 downto 0);
    signal impl_out  : std_logic_vector(7 downto 0);
    signal set_a     : std_logic;
    signal set_x     : std_logic;
    signal set_y     : std_logic;
    signal set_s     : std_logic;
    signal n_out     : std_logic;
    signal v_out     : std_logic;
    signal z_out     : std_logic;
    signal c_out     : std_logic;
    signal d_out     : std_logic;
    signal i_out     : std_logic;
    
    signal opcode    : string(1 to 13);
begin

    mut: entity work.data_oper
    generic map (
        support_bcd => true )
    port map (
        inst       => inst,
                              
        n_in       => n_in,
        v_in       => v_in,
        z_in       => z_in,
        c_in       => c_in,
        d_in       => d_in,
        i_in       => i_in,
                              
        data_in    => data_in,
        a_reg      => a_reg,
        x_reg      => x_reg,
        y_reg      => y_reg,
        s_reg      => s_reg,
                              
        alu_out    => alu_out,
        mem_out    => mem_out,
        impl_out   => impl_out,
                              
        set_a      => set_a,
        set_x      => set_x,
        set_y      => set_y,
        set_s      => set_s,
                              
        n_out      => n_out,
        v_out      => v_out,
        z_out      => z_out,
        c_out      => c_out,
        d_out      => d_out,
        i_out      => i_out );

    process
        procedure write_str(variable L : inout line; s : string) is
        begin
            write(L, s);
        end procedure;

        variable L : line;
    begin
        for i in 0 to 255 loop
            c_in   <= 'U';
            d_in   <= 'U';
            inst   <= conv_std_logic_vector(i, 8);
            opcode <= opcode_array(i);
            wait for 1 us;

            write(L, VecToHex(inst, 2));
            write(L, ' ');
            write(L, opcode_array(i));
            write(L, ':');

            if(n_out /= 'Z') then write(L, 'N');  else write(L, '-'); end if;
            if(v_out /= 'U') then write(L, 'V');  else write(L, '-'); end if;
            if(z_out /= 'U') then write(L, 'Z');  else write(L, '-'); end if;
            if(c_out /= 'U') then write(L, 'C');  else write(L, '-'); end if;
            if(d_out /= 'U') then write(L, 'D');  else write(L, '-'); end if;
            if(i_out /= 'U') then write(L, 'I');  else write(L, '-'); end if;

            c_in   <= '0';
            d_in   <= '0';
            wait for 1 us;

            write(L, ' ');
            if store_a_from_alu(inst) then
                write_str(L, "Store ALU in A ");
            end if;
            if load_x(inst) then
                write_str(L, "Store ALU in X ");
            end if;
            if load_y(inst) then
                write_str(L, "Store ALU in Y ");
            end if;
            if(set_a='1') then
                write_str(L, "A:=");
                write(L, VecToHex(impl_out, 2));
                write_str(L, " ");
            end if;            
            if(set_x='1') then
                write_str(L, "X:=");
                write(L, VecToHex(impl_out, 2));
                write_str(L, " ");
            end if;            
            if(set_y='1') then
                write_str(L, "Y:=");
                write(L, VecToHex(impl_out, 2));
                write_str(L, " ");
            end if;            
            if(set_s='1') then
                write_str(L, "SP:=");
                write(L, VecToHex(impl_out, 2));
                write_str(L, " ");
            end if;

            write_str(L, " ALU: " & VecToHex(alu_out, 2));
            write_str(L, "; MEM: " & VecToHex(alu_out, 2));
            writeline(output, L);
        end loop;
        wait;
    end process;

end tb;

