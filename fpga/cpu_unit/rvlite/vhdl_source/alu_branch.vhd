--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: The alu_branch module is a fully combinatoric implementation of
--              the RISC-V standard integer ALU. Because of the overlap with
--              the branch condition checking logic, this has been merged into
--              this block. The operations include: AND, OR, XOR, ADD, SUB,
--              SLL, SRL, SRA, SLT (Set if less than) and SLTU (SLT for unsigned
--              values. There are some variations possible; see comments in the
--              code. Eventually, the one with the lowest LUT count was chosen.
--
--              Currently this block implements  barrel shifter, which is rather
--              large.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.alu_pkg.all;

entity alu_branch is
port (
    data_1      : in  std_logic_vector(31 downto 0); -- register rs1
    data_2      : in  std_logic_vector(31 downto 0); -- register rs2 or imm
    oper        : in  std_logic_vector(2 downto 0);
    func        : in  std_logic;

    branch_taken: out std_logic;
    data_out    : out std_logic_vector(31 downto 0)
);
end entity;

architecture gideon of alu_branch is
    signal carry        : unsigned(0 downto 0);
    signal data_2_neg   : unsigned(31 downto 0);
    signal result_sum   : std_logic_vector(31 downto 0);
    signal result_log   : std_logic_vector(31 downto 0);
    signal result_xor   : std_logic_vector(31 downto 0);
    signal result_and   : std_logic_vector(31 downto 0);
    signal result_or    : std_logic_vector(31 downto 0); 
    signal less_than    : std_logic_vector(31 downto 0) := (others => '0');
    signal less_than_u  : std_logic_vector(31 downto 0) := (others => '0');
    signal result_left  : std_logic_vector(31 downto 0);
    signal result_right : std_logic_vector(31 downto 0);
    signal uns31_less   : std_logic; 
    signal equal        : std_logic;
begin
    -- Calculate the SUM. SUB is adding the inverse of rs2 plus 1, as a carry
    carry(0) <= func;
    data_2_neg <= unsigned(data_2) when func = '0' else unsigned(not data_2);
    result_sum <= std_logic_vector(unsigned(data_1) + data_2_neg + carry);

    -- Logical Explicit
    -- result_xor <= data_1 xor data_2;
    -- result_and <= data_1 and data_2;
    -- result_or  <= data_1 or  data_2;

    -- Logical submodule
    i_logical: entity work.alu_logical
    port map (
        data_1      => data_1,
        data_2      => data_2,
        oper        => oper(1 downto 0),
        data_out    => result_log );

    result_xor <= result_log;
    result_and <= result_log;
    result_or  <= result_log;

    -- Method 1: Explicit
    -- less_than(0)   <= '1' when signed(data_1) < signed(data_2) else '0';
    -- less_than_u(0) <= '1' when unsigned(data_1) < unsigned(data_2) else '0';

    -- Method 2: First convert to unsigned by shifting the range
    -- (= inverting the upper bit for unsigned mode). Then always compare the unsigned version
    -- uns1_compare <= (data_1(31) xor oper(0)) & unsigned(data_1(30 downto 0));
    -- uns2_compare <= (data_2(31) xor oper(0)) & unsigned(data_2(30 downto 0));
    -- less_than(0) <= '1' when uns1_compare < uns2_compare else '0';
    -- less_than_u <= less_than;
    -- drawback: <S and <U are not available as output signals at the same time 

    -- Method 3: Calculate the less than over the lower 31 bits, then do something
    -- clever with the upper bits and the signed selector:
    -- <S and <U are both available, which can be used for branching 
    uns31_less <= '1' when unsigned(data_1(30 downto 0)) < unsigned(data_2(30 downto 0)) else '0';
    less_than_u(0) <= uns31_less when data_1(31) = data_2(31) else data_2(31); 
    less_than(0)   <= uns31_less when data_1(31) = data_2(31) else not data_2(31);

    -- Barrel func
    result_left <= shift_left(data_1, data_2(4 downto 0));
    result_right <= shift_right(data_1, data_2(4 downto 0), (func and data_1(31)));

    -- -- Barrel Inst
    -- i_barrel: entity work.barrel
    -- port map (
    --     data_in     => data_1,
    --     shamt       => data_2(4 downto 0),
    --     direction   => not oper(2),
    --     arithmetic  => func,
    --     data_out    => result_left
    -- );    
    -- result_right <= result_left;

    with oper select data_out <=
        result_sum   when "000",
        result_left  when "001",
        less_than    when "010",
        less_than_u  when "011",
        result_xor   when "100",
        result_right when "101",
        result_or    when "110",
        result_and   when "111",
        X"00000000"  when others;

    equal <= '1' when data_1 = data_2 else '0';

    with oper select branch_taken <=
        equal              when "000", -- BEQ
        not equal          when "001", -- BNE
        '0'                when "010", -- NOP (illegal for RISCV)
        '1'                when "011", -- BRA (illegal for RISCV)
        less_than(0)       when "100", -- BLT
        not less_than(0)   when "101", -- BGE
        less_than_u(0)     when "110", -- BLTU
        not less_than_u(0) when "111", -- BGEU
        '0'                when others;

end architecture;

