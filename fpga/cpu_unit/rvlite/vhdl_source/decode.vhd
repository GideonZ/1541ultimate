
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;

entity decode is
port
(
    decode_o    : out t_decoded_instruction;
    gprf_o      : out t_gprf_in;
    decode_i    : in  t_fetch_out;
    hazard      : in  std_logic;
    flush       : in  std_logic;
    irq_i       : in  std_logic := '0';
    ena_i       : in std_logic;
    rst_i       : in std_logic;
    clk_i       : in std_logic
);
end entity;

architecture arch of decode is
    signal instruction  : std_logic_vector(31 downto 0);
    signal decoded_c    : t_decoded_instruction;
    signal decoded_r    : t_decoded_instruction;
begin
    instruction <= X"80000073" when irq_i = '1' else decode_i.instruction;

    i_decode_comb: entity work.decode_comb
    port map (
        program_counter => decode_i.program_counter,
        instruction     => instruction,
        inst_valid      => decode_i.inst_valid,
        decoded         => decoded_c
    );

    process(clk_i)
    begin
        if rising_edge(clk_i) then
            if rst_i = '1' then
                decoded_r <= c_decoded_nop; 
            elsif ena_i = '1' and hazard = '0' then
                decoded_r <= decoded_c;
            end if;
            if flush = '1' then
                decoded_r.valid <= '0';
            end if;
        end if;
    end process;

    decode_o <= decoded_r;
    gprf_o.adr_a_i <= decoded_c.reg_rs1 when hazard = '0' else decoded_r.reg_rs1;
    gprf_o.adr_b_i <= decoded_c.reg_rs2 when hazard = '0' else decoded_r.reg_rs2;

end architecture;
