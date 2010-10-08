library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- LUT/FF/S3S/BRAM: 183/66/97/0 x -- 24 bit instruction interface
-- LUT/FF/S3S/BRAM: 192/89/102/0 -- 8 bit instruction interface

library work;
use work.io_bus_pkg.all;

entity iec_processor is
generic (
    g_mhz           : natural := 50);
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- instruction ram interface
    instr_addr      : out unsigned(10 downto 0);
    instr_en        : out std_logic;
    instr_data      : in  std_logic_vector(7 downto 0);

    -- software fifo interface
    up_fifo_full    : in  std_logic;
    up_fifo_put     : out std_logic;
    up_fifo_din     : out std_logic_vector(8 downto 0);
    
    down_fifo_empty : in  std_logic;
    down_fifo_get   : out std_logic;
    down_fifo_flush : out std_logic;
    down_fifo_dout  : in  std_logic_vector(8 downto 0);
    
    clk_o           : out std_logic;
    clk_i           : in  std_logic;
    data_o          : out std_logic;
    data_i          : in  std_logic;
    atn_o           : out std_logic;
    atn_i           : in  std_logic;
    srq_o           : out std_logic;
    srq_i           : in  std_logic );

end iec_processor;

architecture mixed of iec_processor is
    constant c_opc_load     : std_logic_vector(3 downto 0) := X"0";
    constant c_opc_pushc    : std_logic_vector(3 downto 0) := X"1";
    constant c_opc_pushd    : std_logic_vector(3 downto 0) := X"2";
    constant c_opc_pop      : std_logic_vector(3 downto 0) := X"3";
    
    constant c_opc_set      : std_logic_vector(3 downto 0) := X"4";
    constant c_opc_shiftout : std_logic_vector(3 downto 0) := X"5";
    constant c_opc_shiftin  : std_logic_vector(3 downto 0) := X"6";
    constant c_opc_swap     : std_logic_vector(3 downto 0) := X"7";

    constant c_opc_if       : std_logic_vector(3 downto 0) := X"8";
    constant c_opc_if_not   : std_logic_vector(3 downto 0) := X"9";
    constant c_opc_if_data  : std_logic_vector(3 downto 0) := X"A";
    constant c_opc_if_datan : std_logic_vector(3 downto 0) := X"B";

    constant c_opc_wait     : std_logic_vector(3 downto 0) := X"C";
    constant c_opc_wait_not : std_logic_vector(3 downto 0) := X"D";
    constant c_opc_irq_conf : std_logic_vector(3 downto 0) := X"E";
    constant c_opc_if_ctrl  : std_logic_vector(3 downto 0) := X"F";

    signal data_reg     : std_logic_vector(7 downto 0);
    signal inputs       : std_logic_vector(3 downto 0);
    signal drivers      : std_logic_vector(3 downto 0);
    signal timer        : unsigned(11 downto 0);
    signal pc           : unsigned(instr_addr'range);
    signal presc        : integer range 0 to g_mhz;
    signal timer_done   : std_logic;
    signal atn_i_d      : std_logic;
    signal irq_enable   : std_logic;
    signal irq_address  : unsigned(pc'range);
    signal instr_reg    : std_logic_vector(15 downto 0);
    signal valid_reg    : std_logic;
    signal ctrl_reg     : std_logic;
        
    type t_state is (get_inst, get_inst_2, get_inst_3, decode, wait_true, wait_false);
    signal state        : t_state;

    signal instruction  : std_logic_vector(23 downto 0);
    alias a_opcode      : std_logic_vector( 3 downto 0) is instruction(23 downto 20);
    alias a_operand     : std_logic_vector(11 downto 0) is instruction(19 downto 8);
    alias a_mask        : std_logic_vector( 3 downto 0) is instruction(7 downto 4);
    alias a_value       : std_logic_vector( 3 downto 0) is instruction(3 downto 0);
    alias a_databyte    : std_logic_vector( 7 downto 0) is instruction(7 downto 0);

    function extend(i : std_logic_vector; size : integer) return std_logic_vector is
        variable ret    : std_logic_vector(size-1 downto 0) := (others => '0');
    begin
        ret(i'length-1 downto 0) := i;
        return ret;
    end function extend;
begin
    clk_o  <= drivers(0);
    data_o <= drivers(1);
    atn_o  <= drivers(2);
    srq_o  <= drivers(3);
    
    inputs <= srq_i & atn_i & data_i & clk_i;
    
    instr_addr <= pc;
    instr_en   <= '1' when (state = get_inst) or (state = get_inst_2) or (state = get_inst_3) else '0';

    instruction <= extend(instr_data, 24) when instr_data'high=23 else instr_data(7 downto 0) & instr_reg;
    
    process(clock)
        variable v_bit  : std_logic;
    begin
        if rising_edge(clock) then
            up_fifo_put   <= '0';
            down_fifo_get <= '0';
            down_fifo_flush <= '0';
            
            if presc = 0 then
                if timer /= 0 then
                    timer <= timer - 1;
                else
                    timer_done <= '1';
                end if;
                presc <= g_mhz-1;
            else
                presc <= presc - 1;
            end if;

            case state is
            when get_inst =>
                pc <= pc + 1;
                if instr_data'high = 23 then
                    state <= decode;
                else
                    state <= get_inst_2;
                end if;

            when get_inst_2 =>
                instr_reg(7 downto 0) <= instr_data(7 downto 0);
                pc <= pc + 1;
                state <= get_inst_3;
                
            when get_inst_3 =>
                instr_reg(15 downto 8) <= instr_data(7 downto 0);
                pc <= pc + 1;
                state <= decode;

            when decode =>
                timer_done <= '0';
                timer <= unsigned(a_operand);
--                presc <= 0;
                state <= get_inst;
                
                case a_opcode is
                when c_opc_load     =>
                    data_reg <= a_operand(7 downto 0);
                    
                when c_opc_pushc    =>
                    if up_fifo_full='0' then
                        up_fifo_din <= '1' & data_reg;
                        up_fifo_put <= '1';
                    else
                        state <= decode;
                    end if;
                                        
                when c_opc_pushd    =>
                    if up_fifo_full='0' then
                        up_fifo_din <= '0' & data_reg;
                        up_fifo_put <= '1';
                    else
                        state <= decode;
                    end if;

                when c_opc_pop      =>
                    data_reg <= down_fifo_dout(7 downto 0);
                    ctrl_reg <= down_fifo_dout(8);
                    valid_reg <= not down_fifo_empty;
                    
                    if down_fifo_empty='0' then
                        down_fifo_get <= '1';
                    elsif a_databyte(0)='0' then -- empty and non-block bit not set
                        state <= decode;
                    end if;
                                        
                when c_opc_set      =>
                    for i in drivers'range loop
                        if a_mask(i)='1' then
                            drivers(i) <= a_value(i);
                        end if;
                    end loop;
                                        
                when c_opc_shiftout =>
                    for i in drivers'range loop
                        if a_mask(i)='1' then
                            drivers(i) <= data_reg(0) xor a_value(i);
                        end if;
                    end loop;
                    data_reg <= '0' & data_reg(7 downto 1);
                    
                when c_opc_shiftin  =>
                    for i in inputs'range loop
                        if a_mask(i)='1' then
                            v_bit := inputs(i) xor a_value(i);
                            exit;
                        end if;
                    end loop;
                    data_reg <= v_bit & data_reg(7 downto 1);

                when c_opc_swap     =>
                    for i in 0 to 7 loop
                        data_reg(i) <= data_reg(7-i);
                    end loop;

                when c_opc_if       =>
                    if (inputs and a_mask) = a_value then
                        pc <= unsigned(a_operand(pc'range));
                    end if;
                    
                when c_opc_if_not   =>
                    if (inputs and a_mask) /= a_value then
                        pc <= unsigned(a_operand(pc'range));
                    end if;

                when c_opc_if_data  =>
                    if data_reg = a_databyte then
                        pc <= unsigned(a_operand(pc'range));
                    end if;
                    
                when c_opc_if_datan =>
                    if data_reg /= a_databyte then
                        pc <= unsigned(a_operand(pc'range));
                    end if;

                when c_opc_wait     =>
                    state <= wait_true;
                    
                when c_opc_wait_not =>
                    state <= wait_false;

                when c_opc_irq_conf  =>
                    if a_databyte(0)='1' then
                        irq_address <= unsigned(a_operand(pc'range));
                    end if;
                    irq_enable  <= a_databyte(0);
                    
                when c_opc_if_ctrl  => -- data=0 => check on ctrl, data=1 => check on valid
                    if (ctrl_reg='1' and a_databyte(0)='0') or 
                       (valid_reg='1' and a_databyte(0)='1') then
                        pc <= unsigned(a_operand(pc'range));
                    end if;
                                
                when others =>
                    null;
                end case;
    
            when wait_true =>
                if timer_done='1' then
                    state <= get_inst;
                elsif (inputs & a_mask) = a_value then
                    pc <= unsigned(a_operand(pc'range));
                    state <= get_inst;
                end if;

            when wait_false =>
                if timer_done='1' then
                    state <= get_inst;
                elsif (inputs & a_mask) /= a_value then
                    pc <= unsigned(a_operand(pc'range));
                    state <= get_inst;
                end if;

            when others =>
                null;
            end case;

            atn_i_d <= atn_i;
            if atn_i='0' and atn_i_d='1' then
                if irq_enable='1' then
                    down_fifo_flush <= '1';
                    pc <= irq_address;
                    state <= get_inst;
                end if;
            end if;

            if reset='1' then
                state       <= get_inst;
                drivers     <= (others => '1');
                pc          <= (others => '0');
                data_reg    <= (others => '0');
                irq_enable  <= '0';
                irq_address <= (others => '0');
                data_reg    <= (others => '0');
            end if;
        end if;
    end process;
end mixed;


-- CLK (0), DATA (1), ATN (2), SRQ (3)
-- 
-- IRQ_CONFIG      ( ena,  xx, address)  ## enables/dis ATN irq (falling edge causes jump, to address..)
-- IF CTRL         ( select,     jump)   ## sel=0: checks control bit; sel=1: checks valid bit
-- WAIT until      (mask, value, ticks)  ## wait until bits and mask == value, for at maximum ticks ticks (if mask=0 and value/=0 this can be used as plain wait)
-- WAIT until not  (mask, value, ticks)  ## wait until bits and mask /= value, for at maximum ticks ticks (see below)
-- 
-- IF              (mask, value, jump)   ## checks bits and mask == value, if true jump to addr
-- IF not          (mask, value, jump)   ## checks bits and mask == value, if false jump to addr
-- IF DATA         (<mask,value>,jump)   ## 8 bits data are encoded in mask and value together
-- IF DATA not     (<mask,value>,jump)   ## 8 bits data are encoded in mask and value together
-- 
-- SET             (mask, value)         ## changes bits according to mask and value
-- SHIFTOUT        (mask, invert)        ## mask determines what bits are combined (usually just one), invert optionally inverts
-- SHIFTIN         (mask, invert)        ## mask determines what bits are changed on output with out bit, optionally inverted
-- 
-- POP             ()                    ## gets data from fifo, or waits if there are none. (can also be used to sync with software)
-- PUSHD           ()                    ## puts data to fifo
-- PUSHC           ()                    ## puts a control character to fifo (up to software on how to use this)
-- LOAD #          (value)               ## added to output random sequences, or to push status bytes / debug values to fifo
-- 
-- 
-- Instruction encoding: <mask(4)> <value(4)> <ticks/jump (9)> <opcode (4)> = 21 bits
-- 
-- Ticks: All ticks are in 탎. Values > 256 are in steps of 16 탎. (257 = 16탎, 258 = 32탎, ... 511 = 4.08 ms)
-- 
-- Easier to en/decode: 12 bits value operand such that asymetric encoding of time is not necessary.
-- Then total instruction is 24 bits.
-- 
-- 
