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
    signal accu         : unsigned(15 downto 0);
    signal branch_taken : boolean;
    signal n, z, c      : boolean;
    signal push         : std_logic;
    signal pop          : std_logic;
    
    type t_state is (fetch_inst, decode_inst, indirect, external);

    type t_state_vector is record
        state        : t_state;
        i_addr       : unsigned(9 downto 0);
        offset       : unsigned(7 downto 0);
        update_accu  : std_logic;
        update_flags : std_logic;
        alu_oper     : std_logic_vector(14 downto 12);
    end record;

    constant c_default : t_state_vector := (
        state        => fetch_inst,
        i_addr       => (others => '0'),
        offset       => X"00",
        update_accu  => '0',
        update_flags => '0',
        alu_oper     => "000" );
    
    
    signal cur, nxt : t_state_vector;
    signal stack_top    : std_logic_vector(cur.i_addr'range);
    signal ram_en_i     : std_logic;
begin
    with ram_rdata(c_br_eq'range) select branch_taken <=
        z     when c_br_eq,    
        not z when c_br_neq,   
        n     when c_br_mi,    
        not n when c_br_pl,
        c     when c_br_c,
        not c when c_br_nc,    
        true  when c_br_always,
        true  when c_br_call,
        false when others;
        
    io_wdata  <= std_logic_vector(accu);
    ram_wdata <= std_logic_vector(accu);
    io_addr   <= unsigned(ram_rdata(io_addr'range));

    process(ram_rdata, cur, stack_top, branch_taken, stall)
        variable v_inst : std_logic_vector(15 downto 11);
    begin
        ram_we   <= '0';
        ram_en_i <= '0';
        io_write <= '0';
        io_read  <= '0';
        push     <= '0';
        pop      <= '0';
        nxt      <= cur;
        v_inst := ram_rdata(v_inst'range);
        
        case cur.state is
        when fetch_inst =>
            ram_addr <= std_logic_vector(cur.i_addr);
            ram_en_i <= '1';
            nxt.i_addr <= cur.i_addr + 1;
            nxt.state  <= decode_inst;
            nxt.update_accu  <= '0';
            nxt.update_flags <= '0';
            nxt.offset <= X"00";
            
        when decode_inst =>
            nxt.alu_oper <= ram_rdata(cur.alu_oper'range);
            nxt.state <= fetch_inst;
            ram_addr  <= std_logic_vector(unsigned(ram_rdata(ram_addr'range)) + cur.offset);
            
            -- IN instruction
            if v_inst = c_in then
                if ram_rdata(7) = '1' then -- optimization: for ulpi access only
                    nxt.state <= external;
                end if;
                io_read <= '1';
                nxt.update_accu  <= '1';
                nxt.update_flags <= '1';
            -- ALU instruction
            elsif ram_rdata(15) = '0' then
                ram_en_i <= '1';
                nxt.update_accu  <= ram_rdata(11);
                nxt.update_flags <= '1';
            -- BRANCH
            elsif ram_rdata(c_branch'range) = c_branch then
                if branch_taken then
                    nxt.i_addr <= unsigned(ram_rdata(cur.i_addr'range));
                end if;
                if ram_rdata(c_br_call'range) = c_br_call then
                    push <= '1';
                end if;
            -- SPECIALS
            else
                case v_inst is
                when c_store =>
                    ram_we <= '1';
                    ram_en_i <= '1';
                when c_load_ind | c_store_ind =>
                    ram_addr(ram_addr'high downto 3) <= (others => '1');
                    ram_en_i <= '1';
                    nxt.offset <= unsigned(ram_rdata(10 downto 3));
                    nxt.state <= indirect;
                when c_out =>
                    io_write <= '1';
                    if ram_rdata(7) = '1' then -- optimization: for ulpi access only
                        nxt.state <= external;
                    end if;
                when c_return =>                
                    nxt.i_addr <= unsigned(stack_top);
                    pop <= '1';
                when others =>
                    null;
                end case;                
            end if;
            
        when indirect =>
            ram_addr  <= std_logic_vector(unsigned(ram_rdata(ram_addr'range)) + cur.offset);
            ram_en_i  <= '1';
            nxt.state <= fetch_inst;
            -- differentiate between load and store
            if cur.alu_oper = c_alu_load then
                nxt.update_accu  <= '1';
                nxt.update_flags <= '1';
            else
                ram_we    <= '1';
            end if;
            
        when external =>
            ram_addr  <= std_logic_vector(unsigned(ram_rdata(ram_addr'range)) + cur.offset);
            -- differentiate between load and store (read and write)
            if cur.alu_oper = c_alu_ext then
                io_read   <= stall;
                nxt.update_accu  <= '1';
                nxt.update_flags <= '1';
            else
                io_write  <= '0';
            end if;
            nxt.state <= fetch_inst;
            
        when others =>
            nxt.state <= fetch_inst;
        
        end case;
    end process;

    ram_en <= ram_en_i and not stall;

    process(clock)
    begin
        if rising_edge(clock) then
            if reset = '1' then
                cur <= c_default;
            elsif stall='0' then
                cur <= nxt;
            end if;
        end if;
    end process;
    
    i_alu: entity work.nano_alu
    port map (
        clock       => clock,
        reset       => reset,
        
        value_in    => unsigned(ram_rdata),
        ext_in      => unsigned(io_rdata),
        alu_oper    => cur.alu_oper,
        update_accu => cur.update_accu,
        update_flag => cur.update_flags,
        accu        => accu,
        z           => z,
        n           => n,
        c           => c );

    i_stack : entity work.distributed_stack
    generic map (
        width => stack_top'length,
        simultaneous_pushpop => false )     
    port map (
        clock       => clock,
        reset       => reset,
        pop         => pop,
        push        => push,
        flush       => '0',
        data_in     => std_logic_vector(cur.i_addr),
        data_out    => stack_top,
        full        => open,
        data_valid  => open );

end architecture;
