library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;


entity iec_processor is
generic (
    g_mhz           : natural := 50);
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- instruction ram interface
    instr_addr      : out unsigned(8 downto 0);
    instr_en        : out std_logic;
    instr_data      : in  std_logic_vector(29 downto 0);

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

    attribute opt_mode : string;
    attribute opt_mode of iec_processor: entity is "area";

end iec_processor;

architecture mixed of iec_processor is
    constant c_opc_load     : std_logic_vector(3 downto 0) := X"0";
    constant c_opc_pop      : std_logic_vector(3 downto 0) := X"1";
    constant c_opc_pushc    : std_logic_vector(3 downto 0) := X"2";
    constant c_opc_pushd    : std_logic_vector(3 downto 0) := X"3";
    
    constant c_opc_sub      : std_logic_vector(3 downto 0) := X"4";
    constant c_opc_ret      : std_logic_vector(3 downto 0) := X"7";
    constant c_opc_copy_bit : std_logic_vector(3 downto 0) := X"5";

    constant c_opc_if       : std_logic_vector(3 downto 0) := X"8";

    constant c_opc_reset_st : std_logic_vector(3 downto 0) := X"D";
    constant c_opc_reset_drv: std_logic_vector(3 downto 0) := X"E";
    constant c_opc_load_st  : std_logic_vector(3 downto 0) := X"A";
    constant c_opc_load_drv : std_logic_vector(3 downto 0) := X"B";

    constant c_opc_wait     : std_logic_vector(3 downto 0) := X"C";

    signal inputs       : std_logic_vector(3 downto 0);
    signal inputs_raw   : std_logic_vector(3 downto 0);

    signal timer        : unsigned(11 downto 0);
    signal pc           : unsigned(instr_addr'range);
    signal pc_ret       : unsigned(instr_addr'range);
    signal presc        : integer range 0 to g_mhz;
    signal timer_done   : std_logic;
    signal atn_i_d      : std_logic;
    signal valid_reg    : std_logic := '0';
    signal ctrl_reg     : std_logic := '0';
    signal timeout_reg  : std_logic := '0';
        
    type t_state is (idle, get_inst, decode, wait_true);
    signal state        : t_state;

    signal instruction  : std_logic_vector(29 downto 0);
    alias a_invert      : std_logic                     is instruction(29);
    alias a_select      : std_logic_vector( 4 downto 0) is instruction(28 downto 24);
    alias a_opcode      : std_logic_vector( 3 downto 0) is instruction(23 downto 20);
    alias a_operand     : std_logic_vector(11 downto 0) is instruction(19 downto 8);
    alias a_mask        : std_logic_vector( 3 downto 0) is instruction(7 downto 4);
    alias a_value       : std_logic_vector( 3 downto 0) is instruction(3 downto 0);
    alias a_databyte    : std_logic_vector( 7 downto 0) is instruction(7 downto 0);

    signal input_vector : std_logic_vector(31 downto 0);
    signal selected_bit : std_logic;

    signal out_vector   : std_logic_vector(19 downto 0);
    alias a_drivers     : std_logic_vector(3 downto 0) is out_vector(19 downto 16);
    alias a_irq_enable  : std_logic                    is out_vector(8);
    alias a_status      : std_logic_vector(7 downto 0) is out_vector(15 downto 8);
    alias a_data_reg    : std_logic_vector(7 downto 0) is out_vector(7 downto 0);
begin
    clk_o  <= a_drivers(0);
    data_o <= a_drivers(1);
    atn_o  <= a_drivers(2);
    srq_o  <= a_drivers(3);
    
    inputs_raw <= srq_i & atn_i & data_i & clk_i;
    inputs     <= std_logic_vector(to_01(unsigned(inputs_raw)));
    
    input_vector(31 downto 30) <= "10";
    input_vector(29)           <= ctrl_reg;
    input_vector(28)           <= valid_reg;
    input_vector(27)           <= timeout_reg;
    input_vector(26)           <= up_fifo_full;
    input_vector(25)           <= '1' when (inputs and a_mask) = a_value else '0';
    input_vector(24)           <= '1' when (a_data_reg = a_databyte) else '0';
    input_vector(23 downto 20) <= inputs;
    input_vector(19 downto 16) <= a_drivers;
    input_vector(15 downto 8)  <= a_status;
    input_vector(7 downto 0)   <= a_data_reg;

    selected_bit <= input_vector(to_integer(unsigned(a_select))) xor a_invert;
    
    instr_addr <= pc;
    instr_en   <= '1' when (state = get_inst) else '0';

    instruction <= instr_data;
    
    process(clock)
        variable v_bit  : std_logic;
    begin
        if rising_edge(clock) then
            up_fifo_put   <= '0';
            down_fifo_get <= '0';
            down_fifo_flush <= '0';
            
            if presc = 0 then
                if timer = 1 then
                    timer_done <= '1';
                end if;
                if timer /= 0 then
                    timer <= timer - 1;
                end if;
                presc <= g_mhz-1;
            else
                presc <= presc - 1;
            end if;

            case state is
            when idle =>
                null;
                
            when get_inst =>
                pc <= pc + 1;
                state <= decode;

            when decode =>
                timer_done <= '0';
                timer <= unsigned(a_operand);
--                presc <= 0;
                state <= get_inst;
                
                case a_opcode is
                when c_opc_load     =>
                    a_data_reg <= a_databyte;
                    
--                when c_opc_load_st =>
--                    a_status   <= a_databyte(3 downto 0);
--                
--                when c_opc_load_drv =>
--                    a_drivers  <= a_databyte(3 downto 0);
                    
                when c_opc_reset_st =>
                    a_status   <= X"01";
                
                when c_opc_reset_drv =>
                    a_drivers  <= "1111";

                when c_opc_pushc    =>
                    if up_fifo_full='0' then
                        up_fifo_din <= '1' & a_data_reg;
                        up_fifo_put <= '1';
                    else
                        state <= decode;
                    end if;
                                        
                when c_opc_pushd    =>
                    if up_fifo_full='0' then
                        up_fifo_din <= '0' & a_data_reg;
                        up_fifo_put <= '1';
                    else
                        state <= decode;
                    end if;

                when c_opc_pop      =>
                    a_data_reg <= down_fifo_dout(7 downto 0);
                    ctrl_reg   <= down_fifo_dout(8);
                    valid_reg  <= not down_fifo_empty;
                    
                    if down_fifo_empty='0' then
                        down_fifo_get <= '1';
                    elsif a_databyte(0)='0' then -- empty and non-block bit not set
                        state <= decode;
                    end if;

                when c_opc_copy_bit =>
                    out_vector(to_integer(unsigned(a_databyte(4 downto 0)))) <= selected_bit;

                when c_opc_if =>
                    if selected_bit='1' then
                        pc <= unsigned(a_operand(pc'range));
                    end if;
                    
                when c_opc_wait =>
                    timeout_reg <= '0';
                    state <= wait_true;
                    
                when c_opc_sub =>
                    pc_ret <= pc;
                    pc <= unsigned(a_operand(pc'range));
                    
                when c_opc_ret =>
                    pc <= pc_ret;

                when others =>
                    null;
                end case;
    
            when wait_true =>
                if timer_done='1' then
                    state <= get_inst;
                    timeout_reg <= '1';
                elsif selected_bit='1' then
                    state <= get_inst;
                end if;

            when others =>
                null;
            end case;

            atn_i_d <= atn_i;
            if atn_i='0' and atn_i_d/='0' then
                if a_irq_enable='1' then
                    down_fifo_flush <= '1';
                    pc <= (others => '0');
                    state <= get_inst;
                end if;
            end if;

            if reset='1' then
                state        <= idle;
                pc           <= (others => '0');
                pc_ret       <= (others => '0');
                out_vector   <= X"F0100";
            end if;
        end if;
    end process;
end mixed;
