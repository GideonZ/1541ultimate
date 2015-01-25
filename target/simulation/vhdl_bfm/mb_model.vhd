-------------------------------------------------------------------------------
-- Title      : mb_model
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@technolution.eu>
-------------------------------------------------------------------------------
-- Description: Instruction level model of the microblaze
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library work;
    use work.tl_string_util_pkg.all;

library std;
    use std.textio.all;

entity mb_model is
generic (
    g_io_mask   : unsigned(31 downto 0) := X"FC000000" );
port (
    clock               : in  std_logic;
    reset               : in  std_logic;
    
    io_addr             : out unsigned(31 downto 0);
    io_write            : out std_logic;
    io_read             : out std_logic;
    io_byte_en          : out std_logic_vector(3 downto 0);
    io_wdata            : out std_logic_vector(31 downto 0);
    io_rdata            : in  std_logic_vector(31 downto 0);
    io_ack              : in  std_logic ); 

end entity;

architecture bfm of mb_model is
    type t_word_array is array(natural range <>) of std_logic_vector(31 downto 0);
    type t_signed_array is array(natural range <>) of signed(31 downto 0);

    constant c_memory_size_bytes    : natural := 20; -- 1 MB
    constant c_memory_size_words    : natural := c_memory_size_bytes - 2;
    
    shared variable reg  : t_signed_array(0 to 31) := (others => (others => '0'));
    shared variable imem : t_word_array(0 to 2**c_memory_size_words -1);
    alias dmem           : t_word_array(0 to 2**c_memory_size_words -1) is imem;

    signal pc_reg        : unsigned(31 downto 0);
    signal C_flag        : std_logic;
    signal I_flag        : std_logic;
    signal B_flag        : std_logic;
    signal D_flag        : std_logic;

    constant p  : string := "PC: ";
    constant i  : string := ", Inst: ";
    constant c  : string := ", ";
    constant ras    : string := ", Ra=";
    constant rbs    : string := ", Rb=";
    constant rds    : string := ", Rd=";
    
    impure function get_msr(len : natural) return std_logic_vector is
        variable msr    : std_logic_vector(31 downto 0);
    begin
        msr := (31 => C_flag,
                 3 => B_flag,
                 2 => C_flag,
                 1 => I_flag,
                 others => '0' );
        return msr(len-1 downto 0);
    end function;

    function to_std(b : boolean) return std_logic is
    begin
        if b then return '1'; end if;
        return '0';
    end function;

begin
    process
        variable new_pc     : unsigned(31 downto 0);
        variable do_delay   : std_logic := '0';
        variable imm        : signed(31 downto 0);
        variable imm_lock   : std_logic := '0';
        variable ra         : integer range 0 to 31;
        variable rb         : integer range 0 to 31;
        variable rd         : integer range 0 to 31;
        
        procedure dbPrint(pc : unsigned(31 downto 0); inst : std_logic_vector(31 downto 0); str : string) is
            variable s  : line;
        begin
            write(s, p);
            write(s, hstr(pc));
            write(s, i);
            write(s, hstr(inst));
            write(s, ras);
            write(s, hstr(unsigned(reg(ra))));
            write(s, rbs);
            write(s, hstr(unsigned(reg(rb))));
            write(s, rds);
            write(s, hstr(unsigned(reg(rd))));
            write(s, c);
            write(s, str);
--            writeline(output, s);
        end procedure;

        procedure set_msr(a : std_logic_vector(13 downto 0)) is
        begin
            B_flag <= a(3);
            C_flag <= a(2);
            I_flag <= a(1);
        end procedure;    

        procedure perform_add(a, b : signed(31 downto 0); carry : std_logic; upd_c : boolean) is
            variable t33: signed(32 downto 0);
        begin
            if carry = '1' then
                t33 := ('0' & a) + ('0' & b) + 1;
            else
                t33 := ('0' & a) + ('0' & b);
            end if;
            reg(rd) := t33(31 downto 0);
            if upd_c then
                C_flag <= t33(32);
            end if;
        end procedure;

        procedure illegal(pc : unsigned) is
        begin
            report "Illegal instruction @ " & hstr(pc)
            severity error;
        end procedure;
        
        procedure unimplemented(msg : string) is
        begin
            report msg;
        end procedure;
        
        procedure do_branch(pc : unsigned(31 downto 0); offset : signed(31 downto 0); delay, absolute, link : std_logic; chk : integer) is
            variable take : boolean;

        begin
            case chk is
            when 0 => take := (reg(ra) = 0);
            when 1 => take := (reg(ra) /= 0);
            when 2 => take := (reg(ra) < 0);
            when 3 => take := (reg(ra) <= 0);
            when 4 => take := (reg(ra) > 0);
            when 5 => take := (reg(ra) >= 0);
            when others => take := true;
            end case;
            if link='1' then
                reg(rd) := signed(pc);
            end if;
            if take then
                if absolute='1' then
                    new_pc := unsigned(offset);
                else
                    new_pc := unsigned(signed(pc) + offset);
                end if;
            end if; -- else: default: new_pc := pc + 4;
            if take then
                do_delay := delay;
            end if;
        end procedure;

        function is_io(addr : unsigned(31 downto 0)) return boolean is
        begin
            return (addr and g_io_mask) /= 0;
        end function;

        procedure load_byte(addr : unsigned(31 downto 0); data : out std_logic_vector(31 downto 0)) is
            variable loaded     : std_logic_vector(31 downto 0);            
        begin
            if is_io(addr) then
                io_read <= '1';
                io_addr <= unsigned(addr);
                case addr(1 downto 0) is
                when "00" => io_byte_en <= "1000";
                when "01" => io_byte_en <= "0100";
                when "10" => io_byte_en <= "0010";
                when "11" => io_byte_en <= "0001";
                when others => io_byte_en <= "0000";
                end case;
                wait until clock='1';
                io_read <= '0';
                while io_ack = '0' loop
                    wait until clock='1';
                end loop;
                loaded := io_rdata;
            else
                loaded := dmem(to_integer(addr(c_memory_size_bytes-1 downto 2)));
            end if;
            data := (others => '0');
            case addr(1 downto 0) is
            when "00" => data(7 downto 0) := loaded(31 downto 24);
            when "01" => data(7 downto 0) := loaded(23 downto 16);
            when "10" => data(7 downto 0) := loaded(15 downto 8);
            when "11" => data(7 downto 0) := loaded(7 downto 0);
            when others => data := (others => 'X');
            end case;
        end procedure; 

        procedure load_half(addr : unsigned(31 downto 0); data : out std_logic_vector(31 downto 0)) is
            variable loaded     : std_logic_vector(31 downto 0);            
        begin
            if is_io(addr) then
                io_read <= '1';
                io_addr <= unsigned(addr);
                io_byte_en <= (others => '0');
                case addr(1 downto 0) is
                when "00" => io_byte_en <= "1100";
                when "10" => io_byte_en <= "0011";
                when others => null;
                end case;
                wait until clock='1';
                io_read <= '0';
                while io_ack = '0' loop
                    wait until clock='1';
                end loop;
                loaded := io_rdata;
            else
                loaded := dmem(to_integer(addr(c_memory_size_bytes-1 downto 2)));
            end if;
            data := (others => '0');
            case addr(1 downto 0) is
            when "00" => data(15 downto 0) := loaded(31 downto 16);
            when "10" => data(15 downto 0) := loaded(15 downto 0);
            when others => report "Unalligned halfword read" severity error;
            end case;
        end procedure; 

        procedure load_word(addr : unsigned(31 downto 0); data : out std_logic_vector(31 downto 0)) is
            variable loaded     : std_logic_vector(31 downto 0);            
        begin
            if is_io(addr) then
                io_read <= '1';
                io_addr <= unsigned(addr);
                io_byte_en <= (others => '1');
                wait until clock='1';
                io_read <= '0';
                while io_ack = '0' loop
                    wait until clock='1';
                end loop;
                loaded := io_rdata;
            else
                loaded := dmem(to_integer(addr(c_memory_size_bytes-1 downto 2)));
            end if;
            data := loaded;
            assert addr(1 downto 0) = "00"
                report "Unalligned dword read" severity error;
        end procedure; 

        procedure store_byte(addr : unsigned(31 downto 0); data : std_logic_vector(7 downto 0)) is
            variable loaded     : std_logic_vector(31 downto 0);            
        begin
            if is_io(addr) then
                io_write <= '1';
                io_addr <= unsigned(addr);
                io_wdata <= data & data & data & data;
                case addr(1 downto 0) is
                when "00" => io_byte_en <= "1000";
                when "01" => io_byte_en <= "0100";
                when "10" => io_byte_en <= "0010";
                when "11" => io_byte_en <= "0001";
                when others => io_byte_en <= "0000";
                end case;
                wait until clock='1';
                io_write <= '0';
                while io_ack = '0' loop
                    wait until clock='1';
                end loop;
            else
                loaded := dmem(to_integer(addr(c_memory_size_bytes-1 downto 2)));
                case addr(1 downto 0) is
                when "00" => loaded(31 downto 24) := data;
                when "01" => loaded(23 downto 16) := data;
                when "10" => loaded(15 downto 8) := data;
                when "11" => loaded(7 downto 0) := data;
                when others => null;
                end case;
                dmem(to_integer(addr(c_memory_size_bytes-1 downto 2))) := loaded;
            end if;
        end procedure; 

        procedure store_half(addr : unsigned(31 downto 0); data : std_logic_vector(15 downto 0)) is
            variable loaded     : std_logic_vector(31 downto 0);            
        begin
            if is_io(addr) then
                io_write <= '1';
                io_addr <= unsigned(addr);
                io_wdata <= data & data;
                case addr(1 downto 0) is
                when "00" => io_byte_en <= "1100";
                when "10" => io_byte_en <= "0011";
                when others => io_byte_en <= "0000";
                end case;
                wait until clock='1';
                io_write <= '0';
                while io_ack = '0' loop
                    wait until clock='1';
                end loop;
            else
                loaded := dmem(to_integer(addr(c_memory_size_bytes-1 downto 2)));
                case addr(1 downto 0) is
                when "00" => loaded(31 downto 16) := data;
                when "10" => loaded(15 downto 0) := data;
                when others => report "Unalligned halfword write" severity error;
                end case;
                dmem(to_integer(addr(c_memory_size_bytes-1 downto 2))) := loaded;
            end if;
            assert addr(0) = '0'
                report "Unalligned halfword write" severity error;
        end procedure; 

        procedure store_word(addr : unsigned(31 downto 0); data : std_logic_vector(31 downto 0)) is
        begin
            if is_io(addr) then
                io_write <= '1';
                io_addr <= unsigned(addr);
                io_wdata <= data;
                io_byte_en <= "1111";
                wait until clock='1';
                io_write <= '0';
                while io_ack = '0' loop
                    wait until clock='1';
                end loop;
            else
                dmem(to_integer(addr(c_memory_size_bytes-1 downto 2))) := data;
            end if;
            assert addr(1 downto 0) = "00"
                report "Unalligned dword write" severity error;
        end procedure; 

        procedure check_zero(a : std_logic_vector) is
        begin
            assert unsigned(a) = 0
                report "Modifier bits not zero.. Illegal instruction?"
                severity warning;
        end procedure;
        
        procedure dbPrint3(pc : unsigned(31 downto 0); inst : std_logic_vector(31 downto 0); s : string) is
        begin
            dbPrint(pc, inst, s & "R" & str(rd) & ", R" & str(ra) & ", R" & str(rb));
        end procedure;

        procedure dbPrint2(pc : unsigned(31 downto 0); inst : std_logic_vector(31 downto 0); s : string) is
        begin
            dbPrint(pc, inst, s & "R" & str(rd) & ", R" & str(ra));
        end procedure;

        procedure dbPrint2i(pc : unsigned(31 downto 0); inst : std_logic_vector(31 downto 0); s : string) is
        begin
            dbPrint(pc, inst, s & "R" & str(rd) & ", R" & str(ra) & ", 0x" & hstr(unsigned(imm)));
        end procedure;

        procedure dbPrintBr(pc : unsigned(31 downto 0); inst : std_logic_vector(31 downto 0); delay, absolute, link, immediate : std_logic; chk : integer) is
            variable base : string(1 to 6) := "      ";
            variable n : integer := 4;
        begin
            case chk is
            when 0 => base(1 to 3) := "BEQ";
            when 1 => base(1 to 3) := "BNE";
            when 2 => base(1 to 3) := "BLT";
            when 3 => base(1 to 3) := "BLE";
            when 4 => base(1 to 3) := "BGT";
            when 5 => base(1 to 3) := "BGE";
            when others => base(1 to 2) := "BR"; n := 3;
            end case;
            if absolute='1' then base(n) := 'A'; n := n + 1; end if;
            if link='1' then base(n) := 'L'; n := n + 1; end if;
            if immediate='1' then base(n) := 'I'; n := n + 1; end if;
            if delay='1' then base(n) := 'D'; n := n + 1; end if;
            
            if link='1' then
                dbPrint(pc, inst, base & " R" & str(rd) & " => " & hstr(new_pc));
            elsif chk = 15 then
                dbPrint(pc, inst, base & " => " & hstr(new_pc));
            else
                dbPrint(pc, inst, base & " R" & str(ra) & " => " & hstr(new_pc)); 
            end if;                
        end procedure;

        procedure execute_instruction(pc : unsigned(31 downto 0)) is
            variable inst : std_logic_vector(31 downto 0);
            variable data : std_logic_vector(31 downto 0);
            variable temp : std_logic;
        begin
            inst := imem(to_integer(pc(c_memory_size_bytes-1 downto 2)));
            rd := to_integer(unsigned(inst(25 downto 21)));
            ra := to_integer(unsigned(inst(20 downto 16)));
            rb := to_integer(unsigned(inst(15 downto 11)));
            reg(0) := (others => '0');
            imm(15 downto 0) := signed(inst(15 downto 0));
            if imm_lock='0' then
                imm(31 downto 16) := (others => inst(15)); -- sign extend
            end if;
            imm_lock := '0';
            io_write <= '0';
            io_read <= '0';
            io_addr <= (others => '0');
            io_byte_en <= (others => '0');

            new_pc := pc + 4;
            case inst(31 downto 26) is
            when "000000" => -- ADD Rd,Ra,Rb
                dbPrint3(pc, inst, "ADD ");
                perform_add(reg(ra), reg(rb), '0', true);
                check_zero(inst(10 downto 0));

            when "000001" => -- RSUB Rd,Ra,Rb
                dbPrint3(pc, inst, "RSUB ");
                perform_add(not reg(ra), reg(rb), '1', true);
                check_zero(inst(10 downto 0));
                
            when "000010" => -- ADDC Rd,Ra,Rb
                dbPrint3(pc, inst, "ADDC ");
                perform_add(reg(ra), reg(rb), C_flag, true);
                check_zero(inst(10 downto 0));

            when "000011" => -- RSUBC Rd,Ra,Rb
                dbPrint3(pc, inst, "RSUBC ");
                perform_add(not reg(ra), reg(rb), C_flag, true);
                check_zero(inst(10 downto 0));

            when "000100" => -- ADDK Rd,Ra,Rb
                dbPrint3(pc, inst, "ADDK ");
                perform_add(reg(ra), reg(rb), '0', false);
                check_zero(inst(10 downto 0));

            when "000101" => -- RSUBK Rd,Ra,Rb / CMP Rd,Ra,Rb  / CMPU Rd,Ra,Rb
                if inst(1 downto 0) = "01" then -- CMP
                    dbPrint3(pc, inst, "CMP ");
                    temp := not to_std(signed(reg(rb)) >= signed(reg(ra)));
                    perform_add(not reg(ra), reg(rb), '1', false);
                    reg(rd)(31) := temp;
                elsif inst(1 downto 0) = "11" then -- CMPU
                    dbPrint3(pc, inst, "CMPU ");
                    temp := not to_std(unsigned(reg(rb)) >= unsigned(reg(ra)));
                    perform_add(not reg(ra), reg(rb), '1', false);
                    reg(rd)(31) := temp;
                else
                    dbPrint3(pc, inst, "RSUBK ");
                    perform_add(not reg(ra), reg(rb), '1', false);
                end if;
                check_zero(inst(10 downto 2));

            when "000110" => -- ADDKC Rd,Ra,Rb
                dbPrint3(pc, inst, "ADDKC ");
                perform_add(reg(ra), reg(rb), C_flag, false);
                check_zero(inst(10 downto 0));

            when "000111" => -- RSUBKC Rd,Ra,Rb
                dbPrint3(pc, inst, "RSUBKC ");
                perform_add(not reg(ra), reg(rb), C_flag, false);

            when "001000" => -- ADDI Rd,Ra,Imm   
                dbPrint2i(pc, inst, "ADDI ");
                perform_add(reg(ra), imm, '0', true);

            when "001001" => -- RSUBI Rd,Ra,Imm  
                dbPrint2i(pc, inst, "RSUBI ");
                perform_add(not reg(ra), imm, '1', true);
                
            when "001010" => -- ADDIC Rd,Ra,Imm  
                dbPrint2i(pc, inst, "ADDIC ");
                perform_add(reg(ra), imm, C_flag, true);

            when "001011" => -- RSUBIC Rd,Ra,Imm 
                dbPrint2i(pc, inst, "RSUBIC ");
                perform_add(not reg(ra), imm, C_flag, true);

            when "001100" => -- ADDIK Rd,Ra,Imm  
                dbPrint2i(pc, inst, "ADDIK ");
                perform_add(reg(ra), imm, '0', false);

            when "001101" => -- RSUBIK Rd,Ra,Imm 
                dbPrint2i(pc, inst, "RSUBIK ");
                perform_add(not reg(ra), imm, '1', false);

            when "001110" => -- ADDIKC Rd,Ra,Imm 
                dbPrint2i(pc, inst, "ADDIKC ");
                perform_add(reg(ra), imm, C_flag, false);

            when "001111" => -- RSUBIKC Rd,Ra,Imm
                dbPrint2i(pc, inst, "RSUBIKC ");
                perform_add(not reg(ra), imm, C_flag, false);

            when "010000" => -- MUL/MULH/MULHSU/MULHU Rd,Ra,Rb
                unimplemented("MUL/MULH/MULHSU/MULHU Rd,Ra,Rb");
                
            when "010001" => -- BSRA Rd,Ra,Rb / BSLL Rd,Ra,Rb (Barrel shift)
                unimplemented("BSRA Rd,Ra,Rb / BSLL Rd,Ra,Rb (Barrel shift)");

            when "010010" => -- IDIV Rd,Ra,Rb / IDIVU Rd,Ra,Rb
                unimplemented("IDIV Rd,Ra,Rb / IDIVU Rd,Ra,Rb");

            when "010011" => -- 
                illegal(pc);
            when "010100" => -- 
                illegal(pc);
            when "010101" => -- 
                illegal(pc);
            when "010110" => -- 
                illegal(pc);
            when "010111" => -- 
                illegal(pc);
            when "011000" => -- MULI Rd,Ra,Imm                                     
                unimplemented("MULI Rd,Ra,Imm");
            when "011001" => -- BSRLI Rd,Ra,Imm / BSRAI Rd,Ra,Imm / BSLLI Rd,Ra,Imm
                unimplemented("BSRLI Rd,Ra,Imm / BSRAI Rd,Ra,Imm / BSLLI Rd,Ra,Imm");
            when "011010" => -- 
                illegal(pc);
            when "011011" => -- 
                illegal(pc);
            when "011100" => -- 
                illegal(pc);
            when "011101" => -- 
                illegal(pc);
            when "011110" => -- 
                illegal(pc);
            when "011111" => -- 
                illegal(pc);
            when "100000" => -- OR Rd,Ra,Rb  / PCMPBF Rd,Ra,Rb
                if inst(10)='1' then
                    unimplemented("PCMPBF Rd,Ra,Rb");
                else
                    dbPrint3(pc, inst, "OR ");
                    reg(rd) := reg(ra) or reg(rb);
                end if;
                check_zero(inst(9 downto 0));
                                
            when "100001" => -- AND Rd,Ra,Rb
                dbPrint3(pc, inst, "AND ");
                reg(rd) := reg(ra) and reg(rb);
                check_zero(inst(10 downto 0));

            when "100010" => -- XOR Rd,Ra,Rb / PCMPEQ Rd,Ra,Rb
                if inst(10)='1' then
                    unimplemented("PCMPEQ Rd,Ra,Rb");
                else
                    dbPrint3(pc, inst, "XOR ");
                    reg(rd) := reg(ra) xor reg(rb);
                end if;
                check_zero(inst(9 downto 0));

            when "100011" => -- ANDN Rd,Ra,Rb/ PCMPNE Rd,Ra,Rb
                if inst(10)='1' then
                    unimplemented("PCMPNE Rd,Ra,Rb");
                else
                    dbPrint3(pc, inst, "ANDN ");
                    reg(rd) := reg(ra) and not reg(rb);
                end if;
                check_zero(inst(9 downto 0));

            when "100100" => -- SRA Rd,Ra / SRC Rd,Ra / SRL Rd,Ra/ SEXT8 Rd,Ra / SEXT16 Rd,Ra
                case inst(15 downto 0) is
                when X"0001" => -- SRA
                    dbPrint2(pc, inst, "SRA ");
                    C_flag <= reg(ra)(0);
                    reg(rd) := reg(ra)(31) & reg(ra)(31 downto 1);
                    
                when X"0021" => -- SRC
                    dbPrint2(pc, inst, "SRC ");
                    C_flag <= reg(ra)(0);
                    reg(rd) := C_flag & reg(ra)(31 downto 1);

                when X"0041" => -- SRL
                    dbPrint2(pc, inst, "SRL ");
                    C_flag <= reg(ra)(0);
                    reg(rd) := '0' & reg(ra)(31 downto 1);

                when X"0060" => -- SEXT8
                    dbPrint2(pc, inst, "SEXT8 ");
                    reg(rd)(31 downto 8) := (others => reg(ra)(7));
                    reg(rd)(7 downto 0) := reg(ra)(7 downto 0);

                when X"0061" => -- SEXT16
                    dbPrint2(pc, inst, "SEXT16 ");
                    reg(rd)(31 downto 16) := (others => reg(ra)(15));
                    reg(rd)(15 downto 0) := reg(ra)(15 downto 0);

                when others =>
                    illegal(pc);
                    
                end case;
                
            when "100101" => -- MTS Sd,Ra / MFS Rd,Sa / MSRCLR Rd,Imm / MSRSET Rd,Imm
                case inst(15 downto 14) is
                when "00" => -- SET/CLR
                    reg(rd) := get_msr(32);
                    if inst(16)='0' then -- set
                        dbPrint(pc, inst, "MSRSET R" & str(rd) & ", " & hstr(inst(13 downto 0)));
                        set_msr(get_msr(14) or inst(13 downto 0));
                    else -- clear
                        dbPrint(pc, inst, "MSRCLR R" & str(rd) & ", " & hstr(inst(13 downto 0)));
                        set_msr(get_msr(14) and not inst(13 downto 0));
                    end if;
                when "10" => -- MFS (read)
                    dbPrint(pc, inst, "MFS R" & str(rd) & ", " & hstr(inst(13 downto 0)));
                    case inst(15 downto 0) is
                    when X"4000" =>
                        reg(rd) := signed(pc);
                    when X"4001" =>
                        reg(rd) := signed(get_msr(32));
                    when others =>
                        unimplemented("MFS register type " & hstr(inst(13 downto 0)));
                    end case;
                    check_zero(inst(20 downto 16));
                    
                when "11" => -- MTS (write)
                    dbPrint(pc, inst, "MTS R" & str(rd) & ", " & hstr(inst(13 downto 0)));
                    case inst(15 downto 0) is
                    when X"C001" =>
                        set_msr(std_logic_vector(reg(ra)(13 downto 0)));
                    when others =>
                        unimplemented("MTS register type " & hstr(inst(13 downto 0)));
                    end case;
                when others =>
                    illegal(pc);
                end case;

            when "100110" => -- BR(A)(L)(D) (Rb,)Rb / BRK Rd,Rb
                do_branch(pc => pc, offset => reg(rb), delay => inst(20), absolute => inst(19), link => inst(18), chk => 15);
                dbPrintBr(pc => pc, inst => inst, delay => inst(20), absolute => inst(19), link => inst(18), immediate => '0', chk => 15);
                if (inst(20 downto 18) = "011") then
                    B_flag <= '1';
                end if;
                check_zero(inst(10 downto 0));

            when "100111" => -- Bxx Ra,Rb  (Rd = type of branch)
                do_branch(pc => pc, offset => reg(rb), delay => inst(25), absolute => '0', link => '0', chk => to_integer(unsigned(inst(23 downto 21))));
                dbPrintBr(pc => pc, inst => inst, delay => inst(25), absolute => '0', link => '0', immediate => '0', chk => to_integer(unsigned(inst(23 downto 21))));
                check_zero(inst(10 downto 0));

            when "101000" => -- ORI Rd,Ra,Imm
                dbPrint2i(pc, inst, "ORI ");
                reg(rd) := reg(ra) or imm;

            when "101001" => -- ANDI Rd,Ra,Imm
                dbPrint2i(pc, inst, "ANDI ");
                reg(rd) := reg(ra) and imm;

            when "101010" => -- XORI Rd,Ra,Imm
                dbPrint2i(pc, inst, "XORI ");
                reg(rd) := reg(ra) xor imm;

            when "101011" => -- ANDNI Rd,Ra,Imm
                dbPrint2i(pc, inst, "ANDNI ");
                reg(rd) := reg(ra) and not imm;

            when "101100" => -- IMM Imm
                dbPrint(pc, inst, "IMM " & hstr(inst(15 downto 0)));
                imm(31 downto 16) := signed(inst(15 downto 0));
                imm_lock := '1';
                check_zero(inst(25 downto 16));

            when "101101" => -- RTSD Ra,Imm / RTID Ra,Imm / RTBD Ra,Imm / RTED Ra,Imm
                case inst(25 downto 21) is
                    when "10000" =>
                        dbPrint(pc, inst, "RTSD R" & str(ra) & ", " & str(to_integer(imm)));
                        null;                    
                    when "10001" =>
                        dbPrint(pc, inst, "RTID R" & str(ra) & ", " & str(to_integer(imm)));
                        I_flag <= '1';
                    when "10010" =>
                        dbPrint(pc, inst, "RTBD R" & str(ra) & ", " & str(to_integer(imm)));
                        B_flag <= '0';
                    when "10100" =>
                        unimplemented("Return from exception RTED");
                    when others =>
                        illegal(pc);
                end case;
                new_pc := unsigned(reg(ra) + imm);
                do_delay := '1';

            when "101110" => -- BR(A)(L)I(D) Imm Ra / BRKI Rd,Imm
                do_branch(pc => pc, offset => imm, delay => inst(20), absolute => inst(19), link => inst(18), chk => 15);
                dbPrintBr(pc => pc, inst => inst, delay => inst(20), absolute => inst(19), link => inst(18), immediate => '1', chk => 15);
                if (inst(20 downto 18) = "011") then
                    B_flag <= '1';
                end if;

            when "101111" => -- BxxI Ra,Imm (Rd = type of branch)
                do_branch(pc => pc, offset => imm, delay => inst(25), absolute => '0', link => '0', chk => to_integer(unsigned(inst(23 downto 21))));
                dbPrintBr(pc => pc, inst => inst, delay => inst(25), absolute => '0', link => '0', immediate => '1', chk => to_integer(unsigned(inst(23 downto 21))));

            when "110000" => -- LBU Rd,Ra,Rb
                dbPrint3(pc, inst, "LBU ");
                load_byte(unsigned(reg(ra) + reg(rb)), data);
                reg(rd) := signed(data);
                check_zero(inst(10 downto 0));

            when "110001" => -- LHU Rd,Ra,Rb
                dbPrint3(pc, inst, "LHU ");
                load_half(unsigned(reg(ra) + reg(rb)), data);
                reg(rd) := signed(data);
                check_zero(inst(10 downto 0));

            when "110010" => -- LW Rd,Ra,Rb
                dbPrint3(pc, inst, "LW ");
                load_word(unsigned(reg(ra) + reg(rb)), data);
                reg(rd) := signed(data);
                check_zero(inst(10 downto 0));

            when "110011" => -- 
                illegal(pc);

            when "110100" => -- SB Rd,Ra,Rb
                dbPrint3(pc, inst, "SB ");
                store_byte(unsigned(reg(ra) + reg(rb)), std_logic_vector(reg(rd)(7 downto 0)));
                check_zero(inst(10 downto 0));

            when "110101" => -- SH Rd,Ra,Rb
                dbPrint3(pc, inst, "SH ");
                store_half(unsigned(reg(ra) + reg(rb)), std_logic_vector(reg(rd)(15 downto 0)));
                check_zero(inst(10 downto 0));

            when "110110" => -- SW Rd,Ra,Rb
                dbPrint3(pc, inst, "SW ");
                store_word(unsigned(reg(ra) + reg(rb)), std_logic_vector(reg(rd)));
                check_zero(inst(10 downto 0));

            when "110111" => -- 
                illegal(pc);

            when "111000" => -- LBUI Rd,Ra,Imm
                dbPrint2i(pc, inst, "LBUI ");
                load_byte(unsigned(reg(ra) + imm), data);
                reg(rd) := signed(data);
                
            when "111001" => -- LHUI Rd,Ra,Imm
                dbPrint2i(pc, inst, "LHUI ");
                load_half(unsigned(reg(ra) + imm), data);
                reg(rd) := signed(data);

            when "111010" => -- LWI Rd,Ra,Imm 
                dbPrint2i(pc, inst, "LWI ");
                load_word(unsigned(reg(ra) + imm), data);
                reg(rd) := signed(data);

            when "111011" => -- 
                illegal(pc);
                
            when "111100" => -- SBI Rd,Ra,Imm
                dbPrint2i(pc, inst, "SBI ");
                store_byte(unsigned(reg(ra) + imm), std_logic_vector(reg(rd)(7 downto 0)));
                
            when "111101" => -- SHI Rd,Ra,Imm
                dbPrint2i(pc, inst, "SHI ");
                store_half(unsigned(reg(ra) + imm), std_logic_vector(reg(rd)(15 downto 0)));

            when "111110" => -- SWI Rd,Ra,Imm
                dbPrint2i(pc, inst, "SW ");
                store_word(unsigned(reg(ra) + imm), std_logic_vector(reg(rd)));

            when "111111" => -- 
                illegal(pc);
                
            when others =>
                illegal(pc);
            end case;
        end procedure execute_instruction;

        variable old_pc : unsigned(31 downto 0);
    begin
        if reset='1' then
            pc_reg <= (others => '0');
            io_addr <= (others => '0');
            io_wdata <= (others => '0');
            io_read <= '0';
            io_write <= '0';
        else
            D_flag <= '0';
            execute_instruction(pc_reg);
            old_pc := pc_reg;
            pc_reg <= new_pc;
            if do_delay='1' then
                wait until clock='1';
                D_flag <= '1';
                do_delay := '0';
                execute_instruction(old_pc + 4); -- old PC + 4
            end if;
        end if;
        wait until clock='1';
    end process;

end architecture;
