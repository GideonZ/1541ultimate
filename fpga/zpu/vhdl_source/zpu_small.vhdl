------------------------------------------------------------------------------
----                                                                      ----
----  ZPU Small                                                           ----
----                                                                      ----
----  http://www.opencores.org/                                           ----
----                                                                      ----
----  Description:                                                        ----
----  ZPU is a 32 bits small stack cpu. This is the small size version.   ----
----  It doesn't support external memories, needs a dual ported memory.   ----
----                                                                      ----
----  To Do:                                                              ----
----  -                                                                   ----
----                                                                      ----
----  Author:                                                             ----
----    - Øyvind Harboe, oyvind.harboe zylin.com                          ----
----    - Salvador E. Tropea, salvador inti.gob.ar                        ----
----                                                                      ----
------------------------------------------------------------------------------
----                                                                      ----
---- Copyright (c) 2008 Øyvind Harboe <oyvind.harboe zylin.com>           ----
---- Copyright (c) 2008 Salvador E. Tropea <salvador inti.gob.ar>         ----
---- Copyright (c) 2008 Instituto Nacional de Tecnología Industrial       ----
----                                                                      ----
---- Distributed under the BSD license                                    ----
----                                                                      ----
------------------------------------------------------------------------------
----                                                                      ----
---- Design unit:      ZPUSmallCore(Behave) (Entity and architecture)     ----
---- File name:        zpu_small.vhdl                                     ----
---- Note:             None                                               ----
---- Limitations:      None known                                         ----
---- Errors:           None known                                         ----
---- Library:          zpu                                                ----
---- Dependencies:     IEEE.std_logic_1164                                ----
----                   IEEE.numeric_std                                   ----
----                   zpu.zpupkg                                         ----
---- Target FPGA:      Spartan 3 (XC3S1500-4-FG456)                       ----
---- Language:         VHDL                                               ----
---- Wishbone:         No                                                 ----
---- Synthesis tools:  Xilinx Release 9.2.03i - xst J.39                  ----
---- Simulation tools: GHDL [Sokcho edition] (0.2x)                       ----
---- Text editor:      SETEdit 0.5.x                                      ----
----                                                                      ----
------------------------------------------------------------------------------

library IEEE;
use IEEE.std_logic_1164.ALL;
use IEEE.numeric_std.all;

library work;
use work.zpupkg.all;

entity ZPUSmallCore is
   generic(
      WORD_SIZE    : integer:=32;  -- Data width 16/32
      ADDR_W       : integer:=16;  -- Total address space width (incl. I/O)
      MEM_W        : integer:=15;  -- Memory (prog+data+stack) width
      D_CARE_VAL   : std_logic:='X'); -- Value used to fill the unsused bits
   port(
      clk_i        : in  std_logic; -- System Clock
      reset_i      : in  std_logic; -- Synchronous Reset
      interrupt_i  : in  std_logic; -- Interrupt
      break_o      : out std_logic; -- Breakpoint opcode executed
      dbg_o        : out zpu_dbgo_t; -- Debug outputs (i.e. trace log)
      -- BRAM (text, data, bss and stack)
      a_we_o       : out std_logic; -- BRAM A port Write Enable
      a_addr_o     : out unsigned(MEM_W-1 downto WORD_SIZE/16):=(others => '0'); -- BRAM A Address
      a_o          : out unsigned(WORD_SIZE-1 downto 0):=(others => '0'); -- Data to BRAM A port
      a_i          : in  unsigned(WORD_SIZE-1 downto 0); -- Data from BRAM A port
      b_we_o       : out std_logic; -- BRAM B port Write Enable
      b_addr_o     : out unsigned(MEM_W-1 downto WORD_SIZE/16):=(others => '0'); -- BRAM B Address
      b_o          : out unsigned(WORD_SIZE-1 downto 0):=(others => '0'); -- Data to BRAM B port
      b_i          : in  unsigned(WORD_SIZE-1 downto 0); -- Data from BRAM B port
      -- Memory mapped I/O
      mem_busy_i   : in  std_logic;
      data_i       : in  unsigned(WORD_SIZE-1 downto 0);
      data_o       : out unsigned(WORD_SIZE-1 downto 0);
      addr_o       : out unsigned(ADDR_W-1 downto 0);
      write_en_o   : out std_logic;
      read_en_o    : out std_logic);
end entity ZPUSmallCore;

architecture Behave of ZPUSmallCore is
   constant MAX_ADDR_BIT : integer:=ADDR_W-2;
   constant BYTE_BITS    : integer:=WORD_SIZE/16; -- # of bits in a word that addresses bytes
   -- Stack Pointer initial value: BRAM size-8
   constant SP_START_1   : unsigned(ADDR_W-1 downto 0):=to_unsigned((2**MEM_W)-8,ADDR_W);
   constant SP_START     : unsigned(MAX_ADDR_BIT downto BYTE_BITS):=
                           SP_START_1(MAX_ADDR_BIT downto BYTE_BITS);
   constant IO_BIT       : integer:=ADDR_W-1; -- Address bit to determine this is an I/O

   -- Program counter
   signal pc_r           : unsigned(MAX_ADDR_BIT downto 0):=(others => '0');
   -- Stack pointer
   signal sp_r           : unsigned(MAX_ADDR_BIT downto BYTE_BITS):=SP_START;
   signal idim_r         : std_logic:='0';

   -- BRAM (text, data, bss and stack)
   -- a_r is a register for the top of the stack [SP]
   -- Note: as this is a stack CPU this is a very important register.
   signal a_we_r         : std_logic:='0';
   signal a_addr_r       : unsigned(MAX_ADDR_BIT downto BYTE_BITS):=(others => '0');
   signal a_r            : unsigned(WORD_SIZE-1 downto 0):=(others => '0');
   -- b_r is a register for the next value in the stack [SP+1]
   -- We also use the B port to fetch instructions.
   signal b_we_r         : std_logic:='0';
   signal b_addr_r       : unsigned(MAX_ADDR_BIT downto BYTE_BITS):=(others => '0');
   signal b_r            : unsigned(WORD_SIZE-1 downto 0):=(others => '0');

   -- State machine.
   type state_t is (st_fetch, st_write_io_done, st_execute, st_add, st_or,
                    st_and, st_store, st_read_io, st_write_io, st_fetch_next,
                    st_add_sp, st_decode, st_resync);
   signal state          : state_t:=st_resync;
   attribute fsm_encoding : string;
   attribute fsm_encoding of state : signal is "one-hot";
   
   -- Decoded Opcode
   type decode_t is (dec_nop, dec_im, dec_load_sp, dec_store_sp, dec_add_sp,
                     dec_emulate, dec_break, dec_push_sp, dec_pop_pc, dec_add,
                     dec_or, dec_and, dec_load, dec_not, dec_flip, dec_store,
                     dec_pop_sp, dec_interrupt);
   signal d_opcode_r     : decode_t;
   signal d_opcode       : decode_t;

   signal opcode         : unsigned(c_opcode_width-1 downto 0); -- Decoded
   signal opcode_r       : unsigned(c_opcode_width-1 downto 0); -- Registered

   -- IRQ flag
   signal in_irq_r       : std_logic:='0';
   -- I/O space address
   signal addr_r         : unsigned(ADDR_W-1 downto 0):=(others => '0');
begin
   -- Dual ported memory interface
   a_we_o    <= a_we_r;
   a_addr_o  <= a_addr_r(MEM_W-1 downto BYTE_BITS);
   a_o       <= a_r;
   b_we_o    <= b_we_r;
   b_addr_o  <= b_addr_r(MEM_W-1 downto BYTE_BITS);
   b_o       <= b_r;

   -------------------------
   -- Instruction Decoder --
   -------------------------
   -- Note: We use Port B memory to fetch the opcodes.
   decode_control:
   process(b_i, pc_r)
      variable topcode : unsigned(c_opcode_width-1 downto 0);
   begin
      -- Select the addressed byte inside the fetched word
      case (to_integer(pc_r(BYTE_BITS-1 downto 0))) is
           when 0 =>
                topcode:=b_i(31 downto 24);
           when 1 =>
                topcode:=b_i(23 downto 16);
           when 2 =>
                topcode:=b_i(15 downto 8);
           when others => -- 3
                topcode:=b_i(7 downto 0);
      end case;
      opcode <= topcode;

      if (topcode(7 downto 7)=OPCODE_IM) then
         d_opcode <= dec_im;
      elsif (topcode(7 downto 5)=OPCODE_STORESP) then
         d_opcode <= dec_store_sp;
      elsif (topcode(7 downto 5)=OPCODE_LOADSP) then
         d_opcode <= dec_load_sp;
      elsif (topcode(7 downto 5)=OPCODE_EMULATE) then
         d_opcode <= dec_emulate;
      elsif (topcode(7 downto 4)=OPCODE_ADDSP) then
         d_opcode <= dec_add_sp;
      else -- OPCODE_SHORT
         case topcode(3 downto 0) is
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
   end process decode_control;

   data_o <= b_i;
   opcode_control:
   process (clk_i)
      variable sp_offset : unsigned(4 downto 0);
   begin
      if rising_edge(clk_i) then
         break_o      <= '0';
         write_en_o   <= '0';
         read_en_o    <= '0';
         dbg_o.b_inst <= '0';
         if reset_i='1' then
            state    <= st_resync;
            sp_r     <= SP_START;
            pc_r     <= (others => '0');
            idim_r   <= '0';
            a_addr_r <= (others => '0');
            b_addr_r <= (others => '0');
            a_we_r   <= '0';
            b_we_r   <= '0';
            a_r      <= (others => '0');
            b_r      <= (others => '0');
            in_irq_r <= '0';
            addr_r   <= (others => '0');
         else -- reset_i/='1'
            a_we_r <= '0';
            b_we_r <= '0';
            -- This saves LUTs, by explicitly declaring that the
            -- a_o can be left at whatever value if a_we_r is
            -- not set.
            a_r <= (others => D_CARE_VAL);
            b_r <= (others => D_CARE_VAL);
            sp_offset:=(others => D_CARE_VAL);
            a_addr_r   <= (others => D_CARE_VAL);
            b_addr_r   <= (others => D_CARE_VAL);
            addr_r     <= a_i(ADDR_W-1 downto 0);
            d_opcode_r <= d_opcode;
            opcode_r   <= opcode;
            if interrupt_i='0' then
               in_irq_r <= '0'; -- no longer in an interrupt
            end if;
   
            case state is
                 when st_execute =>
                      state <= st_fetch;
                      -- At this point:
                      -- b_i contains opcode word
                      -- a_i contains top of stack
                      pc_r <= pc_r+1;
          
                      -- Debug info (Trace)
                      dbg_o.b_inst <= '1';
                      dbg_o.pc <= (others => '0');
                      dbg_o.pc(MAX_ADDR_BIT downto 0) <= pc_r;
                      dbg_o.opcode <= opcode_r;
                      dbg_o.sp <= (others => '0');
                      dbg_o.sp(MAX_ADDR_BIT downto BYTE_BITS) <= sp_r;
                      dbg_o.stk_a <= a_i;
                      dbg_o.stk_b <= b_i;
       
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
                                a_r       <= (others => D_CARE_VAL);
                                a_r(MAX_ADDR_BIT downto 0) <= pc_r;
                                -- Jump to ISR
                                pc_r <= to_unsigned(32,MAX_ADDR_BIT+1); -- interrupt address
                                --report "ZPU jumped to interrupt!" severity note;
                           when dec_im =>
                                idim_r <= '1';
                                a_we_r <= '1';
                                if idim_r='0' then
                                   -- First IM
                                   -- Push the 7 bits (extending the sign)
                                   sp_r     <= sp_r-1;
                                   a_addr_r <= sp_r-1;
                                   a_r <= unsigned(resize(signed(opcode_r(6 downto 0)),WORD_SIZE));
                                else
                                   -- Next IMs, shift the word and put the new value in the lower
                                   -- bits
                                   a_addr_r <= sp_r;
                                   a_r(WORD_SIZE-1 downto 7) <= a_i(WORD_SIZE-8 downto 0);
                                   a_r(6 downto 0) <= opcode_r(6 downto 0);
                                end if;
                           when dec_store_sp =>
                                -- [SP+Offset]=Pop()
                                b_we_r   <= '1';
                                b_addr_r <= sp_r+sp_offset;
                                b_r      <= a_i;
                                sp_r     <= sp_r+1;
                                state    <= st_resync;
                           when dec_load_sp =>
                                -- Push([SP+Offset])
                                sp_r     <= sp_r-1;
                                a_addr_r <= sp_r+sp_offset;
                           when dec_emulate =>
                                -- Push(PC+1), PC=Opcode[4:0]*32
                                sp_r     <= sp_r-1;
                                a_we_r   <= '1';
                                a_addr_r <= sp_r-1;
                                a_r <= (others => D_CARE_VAL);
                                a_r(MAX_ADDR_BIT downto 0) <= pc_r+1;
                                -- Jump to NUM*32
                                -- The emulate address is:
                                --        98 7654 3210
                                -- 0000 00aa aaa0 0000
                                pc_r <= (others => '0');
                                pc_r(9 downto 5) <= opcode_r(4 downto 0);
                           when dec_add_sp =>
                                -- Push(Pop()+[SP+Offset])
                                a_addr_r <= sp_r;
                                b_addr_r <= sp_r+sp_offset;
                                state    <= st_add_sp;
                           when dec_break =>
                                --report "Break instruction encountered" severity failure;
                                break_o <= '1';
                           when dec_push_sp =>
                                -- Push(SP)
                                sp_r     <= sp_r-1;
                                a_we_r   <= '1';
                                a_addr_r <= sp_r-1;
                                a_r <= (others => D_CARE_VAL);
                                a_r(31) <= '1'; -- for easy comparison with my own version of ZPU
                                a_r(MAX_ADDR_BIT downto BYTE_BITS) <= sp_r;
                           when dec_pop_pc =>
                                -- Pop(PC)
                                pc_r  <= a_i(MAX_ADDR_BIT downto 0);
                                sp_r  <= sp_r+1;
                                state <= st_resync;
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
                           when dec_load =>
                                -- Push([Pop()])
                                if a_i(IO_BIT)='1' then
                                   addr_r    <= a_i(ADDR_W-1 downto 0);
                                   read_en_o <= '1';
                                   state     <= st_read_io;
                                else
                                   a_addr_r <= a_i(MAX_ADDR_BIT downto BYTE_BITS);
                                end if;
                           when dec_not =>
                                -- Push(not(Pop()))
                                a_addr_r <= sp_r(MAX_ADDR_BIT downto BYTE_BITS);
                                a_we_r   <= '1';
                                a_r      <= not a_i;
                           when dec_flip =>
                                -- Push(flip(Pop()))
                                a_addr_r <= sp_r(MAX_ADDR_BIT downto BYTE_BITS);
                                a_we_r   <= '1';
                                for i in 0 to WORD_SIZE-1 loop
                                   a_r(i) <= a_i(WORD_SIZE-1-i);
                                end loop;
                           when dec_store =>
                                -- a=Pop(), b=Pop(), [a]=b
                                b_addr_r <= sp_r+1;
                                sp_r     <= sp_r+1;
                                if a_i(IO_BIT)='1' then
                                   state <= st_write_io;
                                else
                                   state <= st_store;
                                end if;
                           when dec_pop_sp =>
                                -- SP=Pop()
                                sp_r  <= a_i(MAX_ADDR_BIT downto BYTE_BITS);
                                state <= st_resync;
                           when dec_nop =>
                                -- Default, keep addressing to of the stack (A)
                                a_addr_r <= sp_r;
                           when others =>
                                null;
                      end case;
                 when st_read_io =>
                      -- Wait until memory I/O isn't busy
                      if mem_busy_i='0' then
                         state    <= st_fetch;
                         a_we_r   <= '1';
                         a_r      <= data_i;
                      end if;
                 when st_write_io =>
                      -- [A]=B
                      sp_r       <= sp_r+1;
                      write_en_o <= '1';
                      addr_r     <= a_i(ADDR_W-1 downto 0);
                      state      <= st_write_io_done;
                 when st_write_io_done =>
                      -- Wait until memory I/O isn't busy
                      if mem_busy_i='0' then
                         state <= st_resync;
                      end if;
                 when st_fetch =>
                      -- We need to resync. During the *next* cycle
                      -- we'll fetch the opcode @ pc and thus it will
                      -- be available for st_execute the cycle after
                      -- next
                      b_addr_r <= pc_r(MAX_ADDR_BIT downto BYTE_BITS);
                      state    <= st_fetch_next;
                 when st_fetch_next =>
                      -- At this point a_i contains the value that is either
                      -- from the top of stack or should be copied to the top of the stack
                      a_we_r   <= '1';
                      a_r      <= a_i;
                      a_addr_r <= sp_r;
                      b_addr_r <= sp_r+1;
                      state    <= st_decode;
                 when st_decode =>
                      if interrupt_i='1' and in_irq_r='0' and idim_r='0' then
                         -- We got an interrupt, execute interrupt instead of next instruction
                         in_irq_r   <= '1';
                         d_opcode_r <= dec_interrupt;
                      end if;
                      -- during the st_execute cycle we'll be fetching SP+1
                      a_addr_r <= sp_r;
                      b_addr_r <= sp_r+1;
                      state    <= st_execute;
                 when st_store =>
                      sp_r     <= sp_r+1;
                      a_we_r   <= '1';
                      a_addr_r <= a_i(MAX_ADDR_BIT downto BYTE_BITS);
                      a_r      <= b_i;
                      state    <= st_resync;
                 when st_add_sp =>
                      state <= st_add;
                 when st_add =>
                      a_addr_r <= sp_r;
                      a_we_r   <= '1';
                      a_r      <= a_i+b_i;
                      state    <= st_fetch;
                 when st_or =>
                      a_addr_r <= sp_r;
                      a_we_r   <= '1';
                      a_r      <= a_i or b_i;
                      state    <= st_fetch;
                 when st_and =>
                      a_addr_r <= sp_r;
                      a_we_r   <= '1';
                      a_r      <= a_i and b_i;
                      state    <= st_fetch;
                 when st_resync =>
                      a_addr_r <= sp_r;
                      state    <= st_fetch;
                 when others =>
                      null;
            end case;
         end if; -- else reset_i/='1'
      end if; -- rising_edge(clk_i)
   end process opcode_control;
   addr_o <= addr_r;

end architecture Behave; -- Entity: ZPUSmallCore

