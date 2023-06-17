
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;

entity decode is
port
(
    decode_o    : out t_decode_out;
    gprf_o      : out t_gprf_req;
    decode_i    : in  t_fetch_out;
    flush       : in  std_logic;
    hazard      : in  std_logic;
    irq_i       : in  std_logic := '0';
    rdy_i       : in  std_logic;
    rdy_o       : out std_logic;
    reset       : in  std_logic;
    clock       : in  std_logic
);
end entity;

architecture arch of decode is
    signal instruction  : std_logic_vector(31 downto 0);
    signal decoded_c    : t_decode_out;
    signal decoded_r    : t_decode_out := c_decoded_nop;
    signal rdy_o_i      : std_logic;
    signal illegal_inst : std_logic;
    signal illegal      : std_logic;
    signal valid        : std_logic;
begin
    -- instruction <= X"80000073" when irq_i = '1' else decode_i.instruction;
    instruction <= decode_i.instruction;

    i_decode_comb: entity work.decode_comb
    port map (
        interrupt       => irq_i,
        program_counter => decode_i.program_counter,
        instruction     => instruction,
        inst_valid      => decode_i.inst_valid,
        illegal_inst    => illegal_inst,
        decoded         => decoded_c
    );

    illegal <= illegal_inst and decode_i.inst_valid;
    valid <= decode_i.inst_valid;

    process(clock)
    begin
        if rising_edge(clock) then
            if rdy_o_i = '1' then
                decoded_r <= decoded_c;
            end if;
            if flush = '1' or reset = '1' then
                -- when the pipeline needs to be flushed, we simply
                -- tell exec that the instruction is not valid. This
                -- will turn off memory accesses and register writeback.
                decoded_r.valid <= '0';
                -- Keeping the reg_read signals active may cause
                -- a hazard to be detected in a cancelled instruction,
                -- which causes in turn the wrong registers to be read
                -- in the next valid instruction.
                decoded_r.reg_rs1_read <= '0';
                decoded_r.reg_rs2_read <= '0';
            end if;
        end if;
    end process;

    process(decoded_r, hazard)
    begin
        decode_o <= decoded_r;
        if hazard = '1' then
            decode_o.valid <= '0';
        end if;
    end process;

    gprf_o.adr_a_i <= decoded_c.reg_rs1 when hazard = '0' else decoded_r.reg_rs1;
    gprf_o.adr_b_i <= decoded_c.reg_rs2 when hazard = '0' else decoded_r.reg_rs2;
    gprf_o.read_en <= hazard or rdy_i or not decoded_r.valid;

    -- Merge of valids and readys.
    -- Valid down = decode_r.valid and not hazard. Cleared when rdy_i = 1
    -- Ready up = '1' when:
    -- * decoded_r.valid = 0 => new instruction needed
    -- * decoded_o.valid = 1 and rdy_i = 1 => instruction accepted by exec             
    -- -> decoded_o.valid = 1 is equal to decoded_r.valid and not hazard
    rdy_o_i <= not decoded_r.valid or (rdy_i and decoded_r.valid and not hazard);
    rdy_o   <= rdy_o_i;

end architecture;
