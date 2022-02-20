-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- 
-- Author: Gideon Zweijtzer (gideon.zweijtzer (at) gmail.com)
--
-- Note that this file is copyrighted, and is not supposed to be used in other
-- projects without written permission from the author.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;
use work.io_bus_pkg.all;

entity sid_filter is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req := c_io_req_init;
    io_resp     : out t_io_resp;

    filt_co     : in  unsigned(10 downto 0);
    filt_res    : in  unsigned(3 downto 0);

    valid_in    : in  std_logic := '0';
    error_out   : out std_logic;     
    input       : in  signed(17 downto 0);
    high_pass   : out signed(17 downto 0);
    band_pass   : out signed(17 downto 0);
    low_pass    : out signed(17 downto 0);

    valid_out   : out std_logic );
end entity;

architecture dsvf of sid_filter is
    signal filter_q : signed(17 downto 0);
    signal filter_f : signed(17 downto 0);
    signal input_sc : signed(21 downto 0);
    signal filt_ram : std_logic_vector(15 downto 0);
    signal xa           : signed(17 downto 0);
    signal xb           : signed(17 downto 0);
    signal sum_b        : signed(21 downto 0);
    signal sub_a        : signed(21 downto 0);
    signal sub_b        : signed(21 downto 0);
    signal x_reg        : signed(21 downto 0) := (others => '0');
    signal bp_reg       : signed(21 downto 0);
    signal hp_reg       : signed(21 downto 0);
    signal lp_reg       : signed(21 downto 0);
    signal temp_reg     : signed(21 downto 0);
    signal error        : std_logic := '0';
    signal program_counter  : integer range 0 to 15;

    signal instruction  : std_logic_vector(7 downto 0);
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);

    alias  xa_select    : std_logic is instruction(0);
    alias  xb_select    : std_logic is instruction(1);
    alias  sub_a_sel    : std_logic is instruction(2);
    alias  sub_b_sel    : std_logic is instruction(3);
    alias  sum_to_lp    : std_logic is instruction(4);
    alias  sum_to_bp    : std_logic is instruction(5);
    alias  sub_to_hp    : std_logic is instruction(6);
    alias  mult_enable  : std_logic is instruction(7);

    -- operations to execute the filter:
    -- bp_f      = f * bp_reg      
    -- q_contrib = q * bp_reg      
    -- lp        = bp_f + lp_reg   
    -- temp      = input - lp      
    -- hp        = temp - q_contrib
    -- hp_f      = f * hp          
    -- bp        = hp_f + bp_reg   
    -- bp_reg    = bp              
    -- lp_reg    = lp              

    -- x_reg     = f * bp_reg           -- 10000000 -- 80
    -- lp_reg    = x_reg + lp_reg       -- 00010010 -- 12
    -- q_contrib = q * bp_reg           -- 10000001 -- 81
    -- temp      = input - lp           -- 00000000 -- 00 (can be merged with previous!)
    -- hp_reg    = temp - q_contrib     -- 01001100 -- 4C
    -- x_reg     = f * hp_reg           -- 10000010 -- 82
    -- bp_reg    = x_reg + bp_reg       -- 00100000 -- 20

    constant c_program  : t_byte_array := (X"80", X"12", X"81", X"4C", X"82", X"20");

    signal io_coef_en   : std_logic;
    signal io_coef_en_d : std_logic;
begin
    -- Derive the actual 'f' and 'q' parameters
    i_q_table: entity work.Q_table
    port map (
        Q_reg       => filt_res,
        filter_q    => filter_q ); -- 2.16 format

    i_filt_coef: entity work.pseudo_dpram_8x16
    port map (
        rd_clock   => clock,
        rd_address => filt_co(10 downto 1),
        rd_data    => filt_ram,
        rd_en      => '1',

        wr_clock   => clock,
        wr_address => io_req.address(10 downto 0),
        wr_data    => io_req.data,
        wr_en      => io_req.write
    );
    
    io_coef_en   <= io_req.read or io_req.write;
    io_coef_en_d <= io_coef_en when rising_edge(clock);
    io_resp.ack  <= io_coef_en_d;
    io_resp.data <= X"00";

    process(clock)
    begin
        if rising_edge(clock) then
            filter_f <= "00" & signed(filt_ram(15 downto 0));
        end if;
    end process;

    --input_sc <= input;
    input_sc <= shift_right(input, 1) & "0000";

    -- now perform the arithmetic
    xa    <= filter_f when xa_select='0' else filter_q;
    xb    <= bp_reg(21 downto 4) when xb_select='0' else hp_reg (21 downto 4);
    sum_b <= bp_reg   when xb_select='0' else lp_reg;
    sub_a <= input_sc when sub_a_sel='0' else temp_reg;
    sub_b <= lp_reg   when sub_b_sel='0' else x_reg;
    
    process(clock)
        variable x_result   : signed(35 downto 0);
        variable sum_result : signed(21 downto 0);
        variable sub_result : signed(21 downto 0);
    begin
        if rising_edge(clock) then
            x_result := xa * xb;
            if mult_enable='1' then
                x_reg <= x_result(33 downto 12);
                if (x_result(35 downto 33) /= "000") and (x_result(35 downto 33) /= "111") then
                    error <= not error;
                end if;
            end if;

            sum_result := sum_limit(x_reg, sum_b);
            if sum_to_lp='1' then
                lp_reg <= sum_result;
            end if;
            if sum_to_bp='1' then
                bp_reg <= sum_result;
            end if;
            
            sub_result := sub_limit(sub_a, sub_b);
            temp_reg   <= sub_result;
            if sub_to_hp='1' then
                hp_reg <= sub_result;
            end if;

            -- control part
            instruction <= (others => '0');
            if reset='1' then
                hp_reg <= (others => '0');            
                lp_reg <= (others => '0');            
                bp_reg <= (others => '0');            
                program_counter <= 0;
            elsif valid_in = '1' then
                program_counter <= 0;
            else
                if program_counter /= 15 then
                    program_counter <= program_counter + 1;
                end if;
                if program_counter < c_program'length then
                    instruction <= c_program(program_counter);
                end if;
            end if;
            if program_counter = c_program'length then
                valid_out <= '1';
            else
                valid_out <= '0';
            end if;
        end if;
    end process;

    high_pass <= hp_reg(21 downto 4);
    band_pass <= bp_reg(21 downto 4);
    low_pass  <= lp_reg(21 downto 4);
    error_out <= error;
end dsvf;
