------------------------------------------------------------------------------
----                                                                      ----
----  ZPU 8-bit version                                                   ----
----                                                                      ----
----  http://www.opencores.org/                                           ----
----                                                                      ----
----  Description:                                                        ----
----  ZPU is a 32 bits small stack cpu. This is a modified version of     ----
----  the zpu_small implementation. This one has only one 8-bit external  ----
----  memory port, which is used for I/O, instruction fetch and data      ----
----  accesses. It is intended to interface with existing 8-bit systems,  ----
----  while maintaining the large addressing range and 32-bit programming ----
----  model. The 32-bit stack remains "internal" in the ZPU.              ----
----                                                                      ----
----  This version is about the same size as zpu_small from zealot,       ----
----  but performs 25% better at the same clock speed, given that the     ----
----  external memory bus can operate with 0 wait states. The performance ----
----  increase is due to the fact that most instructions only require 3   ----
----  clock cycles instead of 4.                                          ----
----                                                                      ----
----  Author:                                                             ----
----    - Øyvind Harboe, oyvind.harboe zylin.com [zpu concept]            ----
----    - Salvador E. Tropea, salvador inti.gob.ar [zealot]               ----
----    - Gideon Zweijtzer, gideon.zweijtzer technolution.eu [this]       ----
----                                                                      ----
------------------------------------------------------------------------------
----                                                                      ----
---- Copyright (c) 2008 Øyvind Harboe <oyvind.harboe zylin.com>           ----
---- Copyright (c) 2008 Salvador E. Tropea <salvador inti.gob.ar>         ----
---- Copyright (c) 2008 Instituto Nacional de Tecnología Industrial       ----
---- Copyright (c) 2009 Gideon N. Zweijtzer <Technolution.NL>             ----
----                                                                      ----
---- Distributed under the BSD license                                    ----
----                                                                      ----
------------------------------------------------------------------------------
----                                                                      ----
---- Design unit:      zpu_8bit(Behave) (Entity and architecture)         ----
---- File name:        zpu_8bit.vhdl                                      ----
---- Note:             None                                               ----
---- Limitations:      None known                                         ----
---- Errors:           None known                                         ----
---- Library:          work                                               ----
---- Dependencies:     ieee.std_logic_1164                                ----
----                   ieee.numeric_std                                   ----
----                   work.zpupkg                                        ----
---- Target FPGA:      Spartan 3E (XC3S500E-4-PQG208)                     ----
---- Language:         VHDL                                               ----
---- Wishbone:         No                                                 ----
---- Synthesis tools:  Xilinx Release 10.1.03i - xst K.39                 ----
---- Simulation tools: Modelsim                                           ----
---- Text editor:      UltraEdit 11.00a+                                  ----
----                                                                      ----
------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.zpupkg.all;

entity zpu_8bit is
    generic(
        g_addr_size    : integer := 16;  -- Total address space width (incl. I/O)
        g_stack_size   : integer := 12;  -- Memory (stack+data) width
        g_prog_size    : integer := 14;  -- Program size
        g_dont_care    : std_logic := '-'); -- Value used to fill the unsused bits, can be '-' or '0'
    port(
        clk_i        : in  std_logic; -- System Clock
        reset_i      : in  std_logic; -- Synchronous Reset
        interrupt_i  : in  std_logic; -- Interrupt
        break_o      : out std_logic; -- Breakpoint opcode executed
-- synthesis translate_off
        dbg_o        : out zpu_dbgo_t; -- Debug outputs (i.e. trace log)
-- synthesis translate_on
        -- BRAM (stack ONLY)
        a_en_o       : out std_logic;
        a_we_o       : out std_logic; -- BRAM A port Write Enable
        a_addr_o     : out unsigned(g_stack_size-1 downto 2):=(others => '0'); -- BRAM A Address
        a_o          : out unsigned(31 downto 0):=(others => '0'); -- Data to BRAM A port
        a_i          : in  unsigned(31 downto 0); -- Data from BRAM A port

        b_en_o       : out std_logic;
        b_we_o       : out std_logic; -- BRAM B port Write Enable
        b_addr_o     : out unsigned(g_stack_size-1 downto 2):=(others => '0'); -- BRAM B Address
        b_o          : out unsigned(31 downto 0):=(others => '0'); -- Data to BRAM B port
        b_i          : in  unsigned(31 downto 0); -- Data from BRAM B port

        -- memory port for text, bss, data
        c_req_o      : out std_logic; -- request output
        c_inst_o     : out std_logic; -- indicates request is for opcode (program data)
        c_we_o       : out std_logic; -- write
        c_addr_o     : out unsigned(g_addr_size-1 downto 0) := (others => '0');

        c_rack_i     : in  std_logic; -- request acknowledge
        c_dack_i     : in  std_logic; -- data acknowledge (read only)
        
        c_data_i     : in  unsigned(c_opcode_width-1 downto 0);
        c_data_o     : out unsigned(c_opcode_width-1 downto 0) );

end entity zpu_8bit;

architecture Behave of zpu_8bit is
    constant c_max_addr_bit : integer:=g_addr_size-1;
    -- Stack Pointer initial value: BRAM size-8
    constant c_sp_start_1   : unsigned(g_addr_size-1 downto 0):=to_unsigned((2**g_stack_size)-8, g_addr_size);
    constant c_sp_start     : unsigned(g_stack_size-1 downto 2):=
                                    c_sp_start_1(g_stack_size-1 downto 2);

    -- Program counter
    signal pc_r           : unsigned(g_prog_size-1 downto 0):=(others => '0');
    -- Stack pointer
    signal sp_r           : unsigned(g_stack_size-1 downto 2):=c_sp_start;
    signal idim_r         : std_logic:='0';

    -- BRAM (stack)
    -- a_r is a register for the top of the stack [SP]
    -- Note: as this is a stack CPU this is a very important register.
    signal a_we_r         : std_logic:='0';
    signal a_en_r         : std_logic:='0';
    signal a_addr_r       : unsigned(g_stack_size-1 downto 2):=(others => '0');
    signal a_r            : unsigned(31 downto 0):=(others => '0');
    -- b_r is a register for the next value in the stack [SP+1]
    signal b_we_r         : std_logic:='0';
    signal b_en_r         : std_logic:='0';
    signal b_addr_r       : unsigned(g_stack_size-1 downto 2):=(others => '0');
    signal b_r            : unsigned(31 downto 0):=(others => '0');

    signal c_we_r         : std_logic := '0';
    signal c_req_r        : std_logic := '0';
    signal c_mux_r        : std_logic := '0';
    signal c_mux_d        : std_logic := '0';
    
    signal byte_req_cnt   : unsigned(1 downto 0) := "00";
    signal byte_ack_cnt   : unsigned(1 downto 0) := "00";
    
    signal posted_wr_a    : std_logic := '0';

     -- State machine.
    type state_t is (st_fetch, st_execute, st_add, st_or,
                          st_and, st_store, st_read_mem, st_write_mem,
                          st_add_sp, st_decode, st_resync);

    signal state          : state_t:=st_fetch;

    attribute fsm_encoding : string;
    attribute fsm_encoding of state : signal is "one-hot";
    
    -- Decoded Opcode
    type decode_t is (dec_nop, dec_im, dec_load_sp, dec_store_sp, dec_add_sp,
                            dec_emulate, dec_break, dec_push_sp, dec_pop_pc, dec_add,
                            dec_or, dec_and, dec_load, dec_not, dec_flip, dec_store,
                            dec_pop_sp, dec_interrupt, dec_storeb, dec_loadb);
    
    signal d_opcode_r     : decode_t;
    signal d_opcode       : decode_t;

    signal opcode         : unsigned(c_opcode_width-1 downto 0); -- Decoded
    signal opcode_r       : unsigned(c_opcode_width-1 downto 0); -- Registered

    -- IRQ flag
    signal in_irq_r       : std_logic:='0';
    -- I/O space address
    signal addr_r         : unsigned(g_addr_size-1 downto 0):=(others => '0');

begin
    a_en_o    <= a_en_r;
    b_en_o    <= b_en_r;
    c_req_o   <= '1' when state = st_fetch else c_req_r;

    -- Dual ported memory interface
    a_we_o    <= a_we_r;
    a_addr_o  <= a_addr_r(g_stack_size-1 downto 2);
    a_o       <= a_r;
    b_we_o    <= b_we_r;
    b_addr_o  <= b_addr_r(g_stack_size-1 downto 2);
    b_o       <= b_r;

    opcode    <= c_data_i;
    c_addr_o  <= resize(pc_r, g_addr_size) when c_mux_r = '0'
          else   addr_r;
    c_we_o    <= c_we_r;

    -------------------------
    -- Instruction Decoder --
    -------------------------
    -- Note: We use a separate memory port to fetch opcodes.
    decode_control:
    process(opcode)
    begin
-- synthesis translate_off
      if opcode(0)='Z' then
          d_opcode <= dec_nop;
      else
-- synthesis translate_on
        if (opcode(7 downto 7)=OPCODE_IM) then
            d_opcode <= dec_im;
        elsif (opcode(7 downto 5)=OPCODE_STORESP) then
            d_opcode <= dec_store_sp;
        elsif (opcode(7 downto 5)=OPCODE_LOADSP) then
            d_opcode <= dec_load_sp;
        elsif (opcode(7 downto 5)=OPCODE_EMULATE) then
--            if opcode(5 downto 0) = OPCODE_LOADB then
--                d_opcode <= dec_loadb;
--            elsif opcode(5 downto 0) = OPCODE_STOREB then
--                d_opcode <= dec_storeb;
--            else
                d_opcode <= dec_emulate;
--            end if;
        elsif (opcode(7 downto 4)=OPCODE_ADDSP) then
            d_opcode <= dec_add_sp;
        else -- OPCODE_SHORT
            case opcode(3 downto 0) is
                  when OPCODE_BREAK =>
                         d_opcode <= dec_break;
                  when OPCODE_PUSHSP =>
                         d_opcode <= dec_push_sp;
                  when OPCODE_POPPC =>
                         d_opcode <= dec_pop_pc;
                  when OPCODE_ADD =>
                         d_opcode <= dec_add;
                  when OPCODE_OR =>
                         d_opcode <= dec_or;
                  when OPCODE_AND =>
                         d_opcode <= dec_and;
                  when OPCODE_LOAD =>
                         d_opcode <= dec_load;
                  when OPCODE_NOT =>
                         d_opcode <= dec_not;
                  when OPCODE_FLIP =>
                         d_opcode <= dec_flip;
                  when OPCODE_STORE =>
                         d_opcode <= dec_store;
                  when OPCODE_POPSP =>
                         d_opcode <= dec_pop_sp;
                  when others => -- OPCODE_NOP and others
                         d_opcode <= dec_nop;
            end case;
        end if;
-- synthesis translate_off
      end if;
-- synthesis translate_on
    end process decode_control;

    opcode_control:
    process (clk_i)
        variable sp_offset : unsigned(4 downto 0);
    begin
        if rising_edge(clk_i) then
            break_o      <= '0';
-- synthesis translate_off
            dbg_o.b_inst <= '0';
-- synthesis translate_on
            posted_wr_a  <= '0';
            c_we_r       <= '0';

            c_mux_d    <= c_mux_r;
            d_opcode_r <= d_opcode;
            opcode_r   <= opcode;

            a_we_r <= '0';
            b_we_r <= '0';
            a_en_r <= '0';
            b_en_r <= '0';

            a_r <= (others => g_dont_care); -- output register
            b_r <= (others => g_dont_care);
            a_addr_r   <= (others => g_dont_care);
            b_addr_r   <= (others => g_dont_care);

            addr_r(g_addr_size-1 downto 2) <= a_i(g_addr_size-1 downto 2);

            if interrupt_i='0' then
                in_irq_r <= '0'; -- no longer in an interrupt
            end if;
    
            case state is
                when st_fetch =>
                    -- During this cycle
                    -- we'll fetch the opcode @ pc and thus it will
                    -- be available for st_execute in the next cycle

                    -- At this point a_i contains the value that is from the top of the stack
                    -- or that was fetched from the stack with an offset (loadsp)
                    a_r      <= a_i;

                    if c_rack_i='1' then -- our request for instr has been seen
                        -- by default, we need the two values of the stack, so we'll fetch them as well
                        a_we_r   <= posted_wr_a;
                        a_addr_r <= sp_r;
                        a_en_r   <= '1';
                        b_addr_r <= sp_r+1;
                        b_en_r   <= '1';
                        state    <= st_decode;
                    else
                        posted_wr_a <= posted_wr_a; -- hold
                    end if;
                         
                when st_decode =>
                    if c_dack_i='1' then
                        if interrupt_i='1' and in_irq_r='0' and idim_r='0' then
                            -- We got an interrupt, execute interrupt instead of next instruction
                            in_irq_r   <= '1';
                            d_opcode_r <= dec_interrupt; -- override
                        end if;
                        state <= st_execute;
                    end if;

                when st_execute =>
                    state <= st_fetch;
                    -- At this point:
                    -- a_i contains top of stack, b_i contains next-to-top of stack
                    pc_r <= pc_r+1; -- increment by default
         
-- synthesis translate_off
                    -- Debug info (Trace)
                    dbg_o.b_inst <= '1';
                    dbg_o.pc <= (others => '0');
                    dbg_o.pc(g_prog_size-1 downto 0) <= pc_r;
                    dbg_o.opcode <= opcode_r;
                    dbg_o.sp <= (others => '0');
                    dbg_o.sp(g_stack_size-1 downto 2) <= sp_r;
                    dbg_o.stk_a <= a_i;
                    dbg_o.stk_b <= b_i;
-- synthesis translate_on
                    -- During the next cycle we'll be reading the next opcode
                    sp_offset(4):=not opcode_r(4);
                    sp_offset(3 downto 0):=opcode_r(3 downto 0);
      
                    idim_r <= '0';

                    --------------------
                    -- Execution Unit --
                    --------------------
                    case d_opcode_r is
                        when dec_interrupt =>
                            -- Not a real instruction, but an interrupt
                            -- Push(PC); PC=32
                            sp_r      <= sp_r-1;
                            a_addr_r  <= sp_r-1;
                            a_we_r    <= '1';
                            a_en_r    <= '1';
                            a_r       <= (others => g_dont_care);
                            a_r(pc_r'range) <= pc_r;
                            -- Jump to ISR
                            pc_r <= to_unsigned(32, pc_r'length); -- interrupt address
                            --report "ZPU jumped to interrupt!" severity note;

                        when dec_im =>
                            idim_r <= '1';
                            a_we_r <= '1';
                            a_en_r <= '1';
                            if idim_r='0' then
                                -- First IM
                                -- Push the 7 bits (extending the sign)
                                sp_r     <= sp_r-1;
                                a_addr_r <= sp_r-1;
                                a_r <= unsigned(resize(signed(opcode_r(6 downto 0)),32));
                            else
                                -- Next IMs, shift the word and put the new value in the lower
                                -- bits
                                a_addr_r <= sp_r;
                                a_r(31 downto 7) <= a_i(24 downto 0);
                                a_r(6 downto 0) <= opcode_r(6 downto 0);
                            end if;

                        when dec_store_sp =>
                            -- [SP+Offset]=Pop()
                            b_we_r   <= '1';
                            b_en_r   <= '1';
                            b_addr_r <= sp_r+sp_offset;
                            b_r      <= a_i;
                            sp_r     <= sp_r+1;
                            state    <= st_fetch; -- was resync

                        when dec_load_sp =>
                            -- Push([SP+Offset])
                            sp_r        <= sp_r-1;
                            a_addr_r    <= sp_r+sp_offset;
                            a_en_r      <= '1';
                            posted_wr_a <= '1';
                            state       <= st_resync; -- extra delay to fetch from A
                            
                        when dec_emulate =>
                            -- Push(PC+1), PC=Opcode[4:0]*32
                            sp_r     <= sp_r-1;
                            a_we_r   <= '1';
                            a_en_r   <= '1';
                            a_addr_r <= sp_r-1;
                            a_r <= (others => '0'); -- could be changed to don't care
                            a_r(pc_r'range) <= pc_r+1;
                            -- Jump to NUM*32
                            -- The emulate address is:
                            --        98 7654 3210
                            -- 0000 00aa aaa0 0000
                            pc_r <= (others => '0');
                            pc_r(9 downto 5) <= opcode_r(4 downto 0);

                        when dec_add_sp =>
                            -- Push(Pop()+[SP+Offset])
                            b_addr_r <= sp_r+sp_offset;
                            b_en_r   <= '1';
                            state    <= st_add_sp;

                        when dec_break =>
                            --report "Break instruction encountered" severity failure;
                            break_o <= '1';
                            
                        when dec_push_sp =>
                            -- Push(SP)
                            sp_r     <= sp_r-1;
                            a_we_r   <= '1';
                            a_addr_r <= sp_r-1;
                            a_en_r   <= '1';
                            a_r <= (others => '0');
                            a_r(sp_r'range) <= sp_r;
                            a_r(31) <= '1'; -- Mark this address as a stack address

                        when dec_pop_pc =>
                            -- Pop(PC)
                            pc_r  <= a_i(pc_r'range);
                            sp_r  <= sp_r+1;
                            state <= st_fetch; -- was resync

                        when dec_add =>
                            -- Push(Pop()+Pop())
                            sp_r  <= sp_r+1;
                            state <= st_add;

                        when dec_or =>
                            -- Push(Pop() or Pop())
                            sp_r  <= sp_r+1;
                            state <= st_or;

                        when dec_and =>
                            -- Push(Pop() and Pop())
                            sp_r  <= sp_r+1;
                            state <= st_and;

                        when dec_not =>
                            -- Push(not(Pop()))
                            a_addr_r <= sp_r;
                            a_we_r   <= '1';
                            a_en_r   <= '1';
                            a_r      <= not a_i;

                        when dec_flip =>
                            -- Push(flip(Pop()))
                            a_addr_r <= sp_r;
                            a_we_r   <= '1';
                            a_en_r   <= '1';
                            
                            for i in 0 to 31 loop
                                a_r(i) <= a_i(31-i);
                            end loop;
                            
--                        when dec_loadb =>
--                              addr_r    <= a_i(g_addr_size-1 downto 0);
--
--                              assert a_i(31)='0'
--                                report "LoadB only works from external memory!"
--                                severity error;
--                                
--                              c_req_r    <= '1';
--                              c_mux_r   <= '1';
--                              byte_req_cnt  <= "00"; -- 1 byte
--                              byte_cnt_d <= "11";
--                              state     <= st_read_mem;
                            
                        when dec_load =>
                            -- Push([Pop()])
                            addr_r(1 downto 0) <= a_i(1 downto 0);

                            if a_i(31)='1' then -- stack
                                a_addr_r    <= a_i(a_addr_r'range);
                                a_en_r      <= '1';
                                posted_wr_a <= '1';
                                state       <= st_resync;
                            else
                                c_req_r   <= '1'; -- output memory request
                                c_mux_r   <= '1'; -- output correct address
                                state     <= st_read_mem;
                                a_r       <= (others => '0'); -- necessary for one byte reads!
                                if a_i(c_max_addr_bit)='1' then
                                    byte_req_cnt <= "00"; -- 1 byte
                                    byte_ack_cnt <= "00";
                                else
                                    byte_req_cnt <= "11"; -- 4 bytes
                                    byte_ack_cnt <= "11";
                                end if;
                            end if;

                        when dec_store =>
                            -- a=Pop(), b=Pop(), [a]=b
                            sp_r     <= sp_r+1;
                            
                            addr_r(1 downto 0) <= a_i(1 downto 0);

                            if a_i(31) = '1' then
                                state <= st_store;
                            else
                                state <= st_write_mem;
                                if a_i(c_max_addr_bit)='1' then
                                    byte_req_cnt <= "00"; -- 1 byte
                                else
                                    byte_req_cnt <= "11"; -- 4 bytes
                                end if;
                            end if;

                        when dec_pop_sp =>
                            -- SP=Pop()
                            sp_r  <= a_i(g_stack_size-1 downto 2);
                            state <= st_fetch; -- was resync

                        when others => -- includes 'nop'
                              null;
                    end case;
                 
                when st_store =>
                    sp_r     <= sp_r+1; -- for a store we need to pop 2!
                    a_we_r   <= '1';
                    a_en_r   <= '1';
                    a_addr_r <= a_i(g_stack_size-1 downto 2);
                    a_r      <= b_i;
                    state    <= st_fetch; -- was resync

                when st_read_mem =>
                    -- BIG ENDIAN
                    a_r <= a_r; -- stay put, as we are filling it byte by byte!
                    if c_dack_i = '1' then
                        byte_ack_cnt <= byte_ack_cnt - 1;
                        case byte_ack_cnt is
                            when "00" =>
                              a_r(7 downto 0) <= c_data_i;
                              a_addr_r <= sp_r;
                              a_we_r   <= '1';
                              a_en_r   <= '1';
                              state <= st_fetch;
                            when "01" =>
                              a_r(15 downto 8) <= c_data_i;
                            when "10" =>
                              a_r(23 downto 16) <= c_data_i;
                            when others => -- 11
                              a_r(31 downto 24) <= c_data_i;
                        end case;
                    end if;
                    
                    if c_rack_i='1' then                          
                        addr_r(1 downto 0) <= addr_r(1 downto 0) + 1;
                        byte_req_cnt <= byte_req_cnt - 1;
                        if byte_req_cnt = "00" then
                            c_req_r <= '0';                              
                            c_mux_r <= '0';
                        end if;
                    end if;

                when st_write_mem =>
                    c_req_r  <= '1';
                    c_mux_r  <= '1';
                    c_we_r   <= '1';

                    -- Note: Output data is muxed outside of this process
                    if c_rack_i='1' then
                        addr_r(1 downto 0) <= addr_r(1 downto 0) + 1;
                        byte_req_cnt <= byte_req_cnt - 1;
                        if byte_req_cnt = "00" then
                            sp_r     <= sp_r+1; -- add another to sp.
                            c_mux_r  <= '0';
                            c_req_r  <= '0';
                            c_we_r   <= '0';
                            state    <= st_fetch; -- was resync
                        end if;                          
                    end if;

                when st_add_sp =>
                    state <= st_add;

                when st_add =>
                    a_addr_r <= sp_r;
                    a_we_r   <= '1';
                    a_en_r   <= '1';
                    a_r      <= a_i+b_i;
                    state    <= st_fetch;

                when st_or =>
                    a_addr_r <= sp_r;
                    a_we_r   <= '1';
                    a_en_r   <= '1';
                    a_r      <= a_i or b_i;
                    state    <= st_fetch;

                when st_and =>
                    a_addr_r <= sp_r;
                    a_we_r   <= '1';
                    a_en_r   <= '1';
                    a_r      <= a_i and b_i;
                    state    <= st_fetch;

                when st_resync =>
                    a_addr_r    <= sp_r;
                    state       <= st_fetch;
                    posted_wr_a <= posted_wr_a; -- keep

                when others =>
                    null;
            end case;

            if reset_i='1' then
                state    <= st_fetch;
                sp_r     <= c_sp_start;
                pc_r     <= (others => '0');
                idim_r   <= '0';
                in_irq_r <= '0';
                c_mux_r  <= '0';
            end if;

        end if; -- rising_edge(clk_i)
    end process opcode_control;

    p_outmux: process(byte_req_cnt, b_i)
    begin
        case byte_req_cnt is
            when "00" =>
              c_data_o <= b_i(7 downto 0);
            when "01" =>
              c_data_o <= b_i(15 downto 8);
            when "10" =>
              c_data_o <= b_i(23 downto 16);
            when others => -- 11
              c_data_o <= b_i(31 downto 24);
        end case;
    end process;

end architecture Behave; -- Entity: zpu_8bit
