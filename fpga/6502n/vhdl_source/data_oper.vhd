
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.pkg_6502_decode.all;

-- this module puts the alu, shifter and bit/compare unit together

entity data_oper is
generic (
    support_bcd : boolean := true );
port (
    inst            : in  std_logic_vector(7 downto 0);
    
    n_in            : in  std_logic;
    v_in            : in  std_logic;
    z_in            : in  std_logic;
    c_in            : in  std_logic;
    d_in            : in  std_logic;
    i_in            : in  std_logic;
    
    data_in         : in  std_logic_vector(7 downto 0);
    a_reg           : in  std_logic_vector(7 downto 0);
    x_reg           : in  std_logic_vector(7 downto 0);
    y_reg           : in  std_logic_vector(7 downto 0);
    s_reg           : in  std_logic_vector(7 downto 0);
    
    alu_sel_o       : out std_logic_vector(1 downto 0);
    
    alu_out         : out std_logic_vector(7 downto 0);
    mem_out         : out std_logic_vector(7 downto 0);
    mem_c           : out std_logic;
    mem_n           : out std_logic;
    mem_z           : out std_logic;
    impl_out        : out std_logic_vector(7 downto 0);
    
    set_a           : out std_logic;
    set_x           : out std_logic;
    set_y           : out std_logic;
    set_s           : out std_logic;

    flags_imm       : out std_logic;
    n_out           : out std_logic;
    v_out           : out std_logic;
    z_out           : out std_logic;
    c_out           : out std_logic;
    d_out           : out std_logic;
    i_out           : out std_logic );
end data_oper;        

architecture gideon of data_oper is
    signal shift_sel : std_logic_vector(1 downto 0) := "00";
    signal shift_din : std_logic_vector(7 downto 0) := X"00";
    signal shift_dout: std_logic_vector(7 downto 0) := X"00";

    signal alu_sel   : std_logic_vector(1 downto 0) := "00";
    signal alu_data_a: std_logic_vector(7 downto 0) := X"00";
    signal alu_out_i : std_logic_vector(7 downto 0) := X"00";

    signal row0_n    : std_logic;
    signal row0_v    : std_logic;
    signal row0_z    : std_logic;
    signal row0_c    : std_logic;

    signal shft_n    : std_logic;
    signal shft_z    : std_logic;
    signal shft_c    : std_logic;

    signal alu_n     : std_logic;
    signal alu_v     : std_logic;
    signal alu_z     : std_logic;
    signal alu_c     : std_logic;

    signal impl_n    : std_logic;
    signal impl_z    : std_logic;
    signal impl_c    : std_logic;
    signal impl_v    : std_logic;

    signal n, z      : std_logic;
    signal shift_en  : std_logic;
    signal alu_en    : std_logic;

    signal full_carry   : std_logic := '0';
    signal half_carry   : std_logic := '0';
    signal bypass_shift : std_logic;
    signal flag_select  : t_result_select;
begin
    shift_en     <= '1' when is_shift(inst) else '0';
    alu_en       <= '1' when is_alu(inst) else '0';
    bypass_shift <= do_bypass_shift(inst);
    
    flag_select  <= flags_from(inst);
    
    shift_sel <= shifter_in_select(inst);
    with shift_sel select shift_din <=
        data_in           when "01",
        a_reg             when "10",
        data_in and a_reg when "11",
        data_in and (a_reg or X"EE") when others; -- for LAX #$

    alu_sel(0) <= '1' when x_to_alu(inst) else '0';
    alu_sel(1) <= shift_sel(0) and shift_sel(1);
    
    with alu_sel select alu_data_a <=
        a_reg and x_reg   when "01",
        a_reg and data_in when "10",
        a_reg and x_reg and data_in when "11",
        a_reg             when others;
        
    alu_sel_o <= alu_sel;
    
    i_row0: entity work.bit_cpx_cpy 
    port map (
        operation  => inst(7 downto 5),
        
        n_in       => n_in,
        v_in       => v_in,
        z_in       => z_in,
        c_in       => c_in,
        
        data_in    => data_in,
        a_reg      => a_reg,
        x_reg      => x_reg,
        y_reg      => y_reg,
        
        n_out      => row0_n,
        v_out      => row0_v,
        z_out      => row0_z,
        c_out      => row0_c );

    i_shft: entity work.shifter
    port map (
        operation  => inst(7 downto 5),
        bypass     => bypass_shift,
        c_in       => c_in,
        n_in       => n_in,
        z_in       => z_in,
        
        data_in    => shift_din,
        
        c_out      => shft_c,
        n_out      => shft_n,
        z_out      => shft_z,
        
        data_out   => shift_dout );
        
    i_alu: entity work.alu
    generic map (
        support_bcd => support_bcd )
    port map (
        operation   => inst(7 downto 5),
    
        n_in        => n_in,
        v_in        => v_in,
        z_in        => z_in,
        c_in        => c_in,
        d_in        => d_in,
        
        data_a      => alu_data_a,
        data_b      => shift_din,
        
        n_out       => alu_n,
        v_out       => alu_v,
        z_out       => alu_z,
        c_out       => alu_c,

        half_carry  => half_carry,
        full_carry  => full_carry,

        data_out    => alu_out_i );
    
    mem_out <= shift_dout;    

    p_bcd_fixer: process(shift_dout, alu_out_i, d_in, shift_en, alu_en, inst, full_carry, half_carry)
        variable sum_l : unsigned(3 downto 0);
        variable sum_h : unsigned(3 downto 0);
    begin
        if shift_en = '1' then
            sum_h := unsigned(shift_dout(7 downto 4));
            sum_l := unsigned(shift_dout(3 downto 0));
        else
            sum_h := unsigned(alu_out_i(7 downto 4));
            sum_l := unsigned(alu_out_i(3 downto 0));
        end if;

        n <= sum_h(3);
        if sum_h = "0000" and sum_l = "0000" then
            z <= '1';
        else
            z <= '0';
        end if;

        if d_in='1' and support_bcd then
            if alu_en = '1' then
                if inst(7 downto 5)="011" then -- ADC
                    if half_carry = '1' then
                        sum_l := sum_l + 6; 
                    end if;
                    if full_carry = '1' then
                        sum_h := sum_h + 6;
                    end if;
    
                elsif inst(7 downto 5)="111" then -- SBC
                    if half_carry = '0' then
                        sum_l := sum_l - 6;
                    end if;
                    if full_carry = '0' then
                        sum_h := sum_h - 6;
                    end if;
                end if;
            end if;
        end if;
        
        alu_out <= std_logic_vector(sum_h) & std_logic_vector(sum_l);
    end process;

    i_impl: entity work.implied
    port map (
        inst      => inst,
        
        c_in      => c_in,
        i_in      => i_in,
        n_in      => n_in,
        z_in      => z_in,
        d_in      => d_in,
        v_in      => v_in,
        
        reg_a     => a_reg,
        reg_x     => x_reg,
        reg_y     => y_reg,
        reg_s     => s_reg,
        data_in   => data_in,
        
        flags_imm => flags_imm,
        i_out     => i_out,
        d_out     => d_out,
        c_out     => impl_c,
        n_out     => impl_n,
        z_out     => impl_z,
        v_out     => impl_v,
        
        set_a     => set_a,
        set_x     => set_x,
        set_y     => set_y,
        set_s     => set_s,
        
        data_out  => impl_out );
    
    process(flag_select,
            n_in, v_in, z_in, c_in,
            impl_n, impl_v, impl_z, impl_c,
            row0_n, row0_v, row0_z, row0_c,
            alu_n, alu_v, alu_z, alu_c,
            shft_n, shft_z, shft_c, n, z )
    begin
        case flag_select is
        when alu =>
            n_out <= n;
            v_out <= alu_v;
            z_out <= z;
            c_out <= alu_c;
        when impl =>
            n_out <= impl_n;
            v_out <= impl_v;
            z_out <= impl_z;
            c_out <= impl_c;
        when row0 =>
            n_out <= row0_n;
            v_out <= row0_v;
            z_out <= row0_z;
            c_out <= row0_c;
        when shift =>
            n_out <= n;
            v_out <= v_in;
            z_out <= z;
            c_out <= shft_c;
        when others =>
            n_out <= n_in;
            v_out <= v_in;
            z_out <= z_in;
            c_out <= c_in;
        end case;
    end process;
    
    mem_c <= shft_c;
    mem_z <= shft_z;
    mem_n <= shft_n;
     
end gideon;
