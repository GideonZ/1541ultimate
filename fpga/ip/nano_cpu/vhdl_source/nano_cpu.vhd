library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.nano_cpu_pkg.all;

entity nano_cpu is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    -- instruction/data ram
    ram_addr    : out std_logic_vector(9 downto 0);
    ram_en      : out std_logic;
    ram_we      : out std_logic;
    ram_wdata   : out std_logic_vector(15 downto 0);
    ram_rdata   : in  std_logic_vector(15 downto 0);

    -- i/o interface
    io_addr     : out unsigned(7 downto 0);
    io_write    : out std_logic := '0';
    io_read     : out std_logic := '0';
    io_wdata    : out std_logic_vector(15 downto 0);
    io_rdata    : in  std_logic_vector(15 downto 0);
    stall       : in  std_logic );
end entity;

architecture gideon of nano_cpu is
    signal i_addr       : unsigned(9 downto 0);
    signal inst         : std_logic_vector(15 downto 11) := (others => '0');
    signal accu         : unsigned(15 downto 0);
    signal branch_taken : boolean;
    signal n, z         : boolean;
    signal stack_top    : std_logic_vector(i_addr'range);
    signal push         : std_logic;
    signal pop          : std_logic;
    signal update_accu  : std_logic;
    signal update_flag  : std_logic;
    signal long_inst    : std_logic;
    
    type t_state is (fetch_inst, decode_inst, data_state, external_data);
    signal state        : t_state;
begin
    with ram_rdata(c_br_eq'range) select branch_taken <=
        z     when c_br_eq,    
        not z when c_br_neq,   
        n     when c_br_mi,    
        not n when c_br_pl,    
        true  when c_br_always,
        true  when c_br_call,
        false when others;
        
    with state select ram_addr <=
        std_logic_vector(i_addr) when fetch_inst,
        ram_rdata(ram_addr'range) when others;

    io_wdata  <= std_logic_vector(accu);
    ram_wdata <= std_logic_vector(accu);
    ram_en    <= '0' when (state = decode_inst) and (ram_rdata(c_store'range) = c_store) else not stall;
    
    push <= '1' when (state = decode_inst) and (ram_rdata(c_br_call'range) = c_br_call) and (ram_rdata(c_branch'range) = c_branch) else '0';
    pop  <= '1' when (state = decode_inst) and (ram_rdata(c_return'range) = c_return) else '0';
    
    with ram_rdata(inst'range) select long_inst <= 
        '1' when c_store,
        '1' when c_load_ind,
        '1' when c_store_ind,
        '0' when others;
    
    process(clock)
    begin
        if rising_edge(clock) then
            if stall='0' then
                io_addr     <= unsigned(ram_rdata(io_addr'range));
                update_accu <= '0';
                update_flag <= '0';
            end if;
            io_write    <= '0';
            io_read     <= '0';
            ram_we      <= '0';
                                                
            case state is
            when fetch_inst =>
                i_addr <= i_addr + 1;
                state <= decode_inst;
                
            when decode_inst =>
                state <= fetch_inst;
                inst <= ram_rdata(inst'range);
                update_accu <= ram_rdata(11);
                update_flag <= not ram_rdata(15);

                -- special instructions
                if ram_rdata(c_in'range) = c_in then
                    io_read <= '1';
                    state <= external_data;
                elsif ram_rdata(c_branch'range) = c_branch then
                    update_accu <= '0';
                    update_flag <= '0';
                    if branch_taken then
                        i_addr <= unsigned(ram_rdata(i_addr'range));
                    end if;
                elsif ram_rdata(c_return'range) = c_return then
                    i_addr <= unsigned(stack_top);
                    update_accu <= '0';
                    update_flag <= '0';
                elsif ram_rdata(c_out'range) = c_out then
                    io_write <= '1';
                    if ram_rdata(7) = '1' then -- optimization: for ulpi access only
                        state <= external_data;
                    end if;
                -- not so special instructions: alu instructions!
                else
                    if long_inst='1' then
                        state <= data_state;
                    end if;
                    if ram_rdata(c_store'range) = c_store then
                        ram_we <= '1';
                    end if;
                    if ram_rdata(c_store_ind'range) = c_store_ind then
                        ram_we <= '1';
                    end if;
                end if;
                    
            when external_data =>
                if stall = '0' then
                    update_accu <= '0';
                    update_flag <= '0';
                    state <= fetch_inst;
                end if;
                
            when data_state =>
                if inst = c_load_ind then
                    update_accu <= '1';
                    update_flag <= '1';
                end if;
                state <= fetch_inst;
                            
            when others =>
                state <= fetch_inst;
            
            end case;
            
            if reset='1' then
                state  <= fetch_inst;
                i_addr <= (others => '0');
            end if;                
        end if;
    end process;
    
    i_alu: entity work.nano_alu
    port map (
        clock       => clock,
        reset       => reset,
        
        value_in    => unsigned(ram_rdata),
        ext_in      => unsigned(io_rdata),
        alu_oper    => inst(14 downto 12),
        update_accu => update_accu,
        update_flag => update_flag,
        accu        => accu,
        z           => z,
        n           => n );

    i_stack : entity work.distributed_stack
    generic map (
        width => i_addr'length,
        simultaneous_pushpop => false )     
    port map (
        clock       => clock,
        reset       => reset,
        pop         => pop,
        push        => push,
        flush       => '0',
        data_in     => std_logic_vector(i_addr),
        data_out    => stack_top,
        full        => open,
        data_valid  => open );

end architecture;
