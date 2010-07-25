------------------------------------------------------------------------------
----                                                                      ----
----  ZPU Medium                                                          ----
----                                                                      ----
----  http://www.opencores.org/                                           ----
----                                                                      ----
----  Description:                                                        ----
----  ZPU is a 32 bits small stack cpu. This is the medium size version.  ----
----  Supports external memories.                                         ----
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
---- Design unit:      ZPUMediumCore(Behave) (Entity and architecture)    ----
---- File name:        zpu_medium.vhdl                                    ----
---- Note:             None                                               ----
---- Limitations:      None known                                         ----
---- Errors:           None known                                         ----
---- Library:          zpu                                                ----
---- Dependencies:     IEEE.std_logic_1164                                ----
----                   IEEE.numeric_std                                   ----
----                   zpu.zpupkg                                         ----
---- Target FPGA:      Spartan 3 (XC3S400-4-FT256)                        ----
---- Language:         VHDL                                               ----
---- Wishbone:         No                                                 ----
---- Synthesis tools:  Xilinx Release 9.2.03i - xst J.39                  ----
---- Simulation tools: GHDL [Sokcho edition] (0.2x)                       ----
---- Text editor:      SETEdit 0.5.x                                      ----
----                                                                      ----
------------------------------------------------------------------------------
--
-- write_en_o   - set to '1' for a single cycle to send off a write request.
--                data_o is valid only while write_en_o='1'.
-- read_en_o    - set to '1' for a single cycle to send off a read request.
-- mem_busy_i   - It is illegal to send off a read/write request when
--                mem_busy_i='1'.
--                Set to '0' when data_i  is valid after a read request.
--                If it goes to '1'(busy), it is on the cycle after read/
--                write_en_o is '1'.
-- addr_o       - address for read/write request
-- data_i       - read data. Valid only on the cycle after mem_busy_i='0'
--                after read_en_o='1' for a single cycle.
-- data_o       - data to write
-- break_o      - set to '1' when CPU hits break instruction

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

library zpu;
use zpu.zpupkg.all;

entity ZPUMediumCore is
   generic(
      WORD_SIZE    : integer:=32;  -- 16/32 (2**wordPower)
      ADDR_W       : integer:=16;  -- Total address space width (incl. I/O)
      MEM_W        : integer:=15;  -- Memory (prog+data+stack) width
      D_CARE_VAL   : std_logic:='X'; -- Value used to fill the unsused bits
      MULT_PIPE    : boolean:=false; -- Pipeline multiplication
      BINOP_PIPE   : integer range 0 to 2:=0; -- Pipeline binary operations (-, =, < and <=)
      ENA_LEVEL0   : boolean:=true;  -- eq, loadb, neqbranch and pushspadd
      ENA_LEVEL1   : boolean:=true;  -- lessthan, ulessthan, mult, storeb, callpcrel and sub
      ENA_LEVEL2   : boolean:=false; -- lessthanorequal, ulessthanorequal, call and poppcrel
      ENA_LSHR     : boolean:=true;  -- lshiftright
      ENA_IDLE     : boolean:=false; -- Enable the enable_i input
      FAST_FETCH   : boolean:=true); -- Merge the st_fetch with the st_execute states
   port(
      clk_i        : in  std_logic; -- CPU Clock
      reset_i      : in  std_logic; -- Sync Reset
      enable_i     : in  std_logic; -- Hold the CPU (after reset)
      break_o      : out std_logic; -- Break instruction executed
      dbg_o        : out zpu_dbgo_t; -- Debug outputs (i.e. trace log)
      -- Memory interface
      mem_busy_i   : in  std_logic; -- Memory is busy
      data_i       : in  unsigned(WORD_SIZE-1 downto 0); -- Data from mem
      data_o       : out unsigned(WORD_SIZE-1 downto 0); -- Data to mem
      addr_o       : out unsigned(ADDR_W-1 downto 0); -- Memory address
      write_en_o   : out std_logic;  -- Memory write enable
      read_en_o    : out std_logic); -- Memory read enable
end entity ZPUMediumCore;

architecture Behave of ZPUMediumCore is
   constant BYTE_BITS    : integer:=WORD_SIZE/16; -- # of bits in a word that addresses bytes
   constant WORD_BYTES   : integer:=WORD_SIZE/OPCODE_W;
   constant MAX_ADDR_BIT : integer:=ADDR_W-2;
   -- Stack Pointer initial value: BRAM size-8
   constant SP_START_1   : unsigned(ADDR_W-1 downto 0):=to_unsigned((2**MEM_W)-8,ADDR_W);
   constant SP_START     : unsigned(ADDR_W-1 downto BYTE_BITS):=
                           SP_START_1(ADDR_W-1 downto BYTE_BITS);

   -- Update [SP+1]. We hold it in b_r, this writes the value to memory.
   procedure FlushB(signal we     : out std_logic;
                    signal addr   : out unsigned(ADDR_W-1 downto BYTE_BITS);
                    signal inc_sp : in  unsigned(ADDR_W-1 downto BYTE_BITS);
                    signal data   : out unsigned(WORD_SIZE-1 downto 0);
                    signal b      : in  unsigned(WORD_SIZE-1 downto 0)) is
   begin
      we   <= '1';
      addr <= inc_sp;
      data <= b;
   end procedure FlushB;

   -- Do a simple stack push, it is performed in the internal cache registers,
   -- not in the real memory.
   procedure Push(signal sp     : inout unsigned(ADDR_W-1 downto BYTE_BITS);
                  signal a      : in    unsigned(WORD_SIZE-1 downto 0);
                  signal b      : out   unsigned(WORD_SIZE-1 downto 0)) is
   begin
      b  <= a;      -- Update cache [SP+1]=[SP]
      sp <= sp-1;
   end procedure Push;

   -- Do a simple stack pop, it is performed in the internal cache registers,
   -- not in the real memory.
   procedure Pop(signal sp     : inout unsigned(ADDR_W-1 downto BYTE_BITS);
                 signal a      : out   unsigned(WORD_SIZE-1 downto 0);
                 signal b      : in    unsigned(WORD_SIZE-1 downto 0)) is
   begin
      a  <= b;      -- Update cache [SP]=[SP+1]
      sp <= sp+1;
   end procedure Pop;

   -- Expand a PC value to WORD_SIZE
   function ExpandPC(v : unsigned(ADDR_W-1 downto 0)) return unsigned is
      variable nv : unsigned(WORD_SIZE-1 downto 0);
   begin
      nv:=(others => '0');
      nv(ADDR_W-1 downto 0):=v;
      return nv;
   end function ExpandPC;

   -- Program counter
   signal pc_r          : unsigned(ADDR_W-1 downto 0):=(others => '0');
   -- Stack pointer
   signal sp_r          : unsigned(ADDR_W-1 downto BYTE_BITS):=SP_START;
   -- SP+1, SP+2 and SP-1 are very used, these are shortcuts
   signal inc_sp        : unsigned(ADDR_W-1 downto BYTE_BITS);
   signal inc_inc_sp    : unsigned(ADDR_W-1 downto BYTE_BITS);
   -- a_r is a cache for the top of the stack [SP]
   -- Note: as this is a stack CPU this is a very important register.
   signal a_r           : unsigned(WORD_SIZE-1 downto 0);
   -- b_r is a cache for the next value in the stack [SP+1]
   signal b_r           : unsigned(WORD_SIZE-1 downto 0);
   signal bin_op_res1_r : unsigned(WORD_SIZE-1 downto 0):=(others => '0');
   signal bin_op_res2_r : unsigned(WORD_SIZE-1 downto 0):=(others => '0');
   signal mult_res1_r   : unsigned(WORD_SIZE-1 downto 0);
   signal mult_res2_r   : unsigned(WORD_SIZE-1 downto 0);
   signal mult_res3_r   : unsigned(WORD_SIZE-1 downto 0);
   signal mult_a_r      : unsigned(WORD_SIZE-1 downto 0):=(others => '0');
   signal mult_b_r      : unsigned(WORD_SIZE-1 downto 0):=(others => '0');
   signal idim_r        : std_logic;
   signal write_en_r    : std_logic;
   signal read_en_r     : std_logic;
   signal addr_r        : unsigned(ADDR_W-1 downto BYTE_BITS):=(others => '0');
   signal fetched_w_r   : unsigned(WORD_SIZE-1 downto 0);

   type state_t is(st_load2, st_popped, st_load_sp2, st_load_sp3, st_add_sp2,
                   st_fetch, st_execute, st_decode, st_decode2, st_resync,
                   st_store_sp2, st_resync2, st_resync3, st_loadb2, st_storeb2,
                   st_mult2, st_mult3, st_mult5, st_mult4, st_binary_op_res2,
                   st_binary_op_res, st_idle); 
   signal state : state_t:=st_resync;

   -- Go to st_fetch state or just do its work
   procedure DoFetch(constant FAST   : boolean;
                     signal state    : out state_t;
                     signal addr     : out unsigned(ADDR_W-1 downto BYTE_BITS);
                     signal pc       : in  unsigned(ADDR_W-1 downto 0);
                     signal re       : out std_logic;
                     signal busy     : in  std_logic) is
   begin
      if FAST then
         -- Equivalent to st_fetch
         if busy='0' then
            addr  <= pc(ADDR_W-1 downto BYTE_BITS);
            re    <= '1';
            state <= st_decode;
         end if;
      else
         state <= st_fetch;
      end if;
   end procedure DoFetch;

   -- Perform a "binary operation" (2 operands)
   procedure DoBinOp(result         : in    unsigned(WORD_SIZE-1 downto 0);
                     signal state   : out   state_t;
                     signal sp      : inout unsigned(ADDR_W-1 downto BYTE_BITS);
                     signal addr    : out   unsigned(ADDR_W-1 downto BYTE_BITS);
                     signal re      : out   std_logic;
                     signal dest    : out   unsigned(WORD_SIZE-1 downto 0);
                     signal dest_p  : out   unsigned(WORD_SIZE-1 downto 0);
                     constant DEPTH :       natural) is
   begin
      if DEPTH=2 then
         -- 2 clocks: st_binary_op_res+st_binary_op_res2
         state  <= st_binary_op_res;
         dest_p <= result;
      elsif DEPTH=1 then
         -- 1 clock: st_binary_op_res2
         state  <= st_binary_op_res2;
         dest_p <= result;
      else -- 0 clocks
         re    <= '1';
         addr  <= sp+2;
         sp    <= sp+1;
         dest  <= result;
         state <= st_popped;
      end if;
   end procedure DoBinOp;

   -- Perform a boolean "binary operation" (2 operands)
   procedure DoBinOpBool(result         : in    boolean;
                         signal state   : out   state_t;
                         signal sp      : inout unsigned(ADDR_W-1 downto BYTE_BITS);
                         signal addr    : out   unsigned(ADDR_W-1 downto BYTE_BITS);
                         signal re      : out   std_logic;
                         signal dest    : out   unsigned(WORD_SIZE-1 downto 0);
                         signal dest_p  : out   unsigned(WORD_SIZE-1 downto 0);
                         constant DEPTH :       natural) is
      variable res : unsigned(WORD_SIZE-1 downto 0):=(others => '0');
   begin
      if result then
         res(0):='1';
      end if;
      DoBinOp(res,state,sp,addr,re,dest,dest_p,DEPTH);
   end procedure DoBinOpBool;

   type insn_t is (dec_add_top, dec_dup, dec_dup_stk_b, dec_pop, dec_add,
                   dec_or, dec_and, dec_store, dec_add_sp, dec_shift, dec_nop,
                   dec_im, dec_load_sp, dec_store_sp, dec_emulate, dec_load,
                   dec_push_sp, dec_pop_pc, dec_pop_pc_rel, dec_not, dec_flip,
                   dec_pop_sp, dec_neq_branch, dec_eq, dec_loadb, dec_mult,
                   dec_less_than, dec_less_than_or_equal, dec_lshr,
                   dec_u_less_than_or_equal, dec_u_less_than, dec_push_sp_add,
                   dec_call, dec_call_pc_rel, dec_sub, dec_break, dec_storeb,
                   dec_insn_fetch, dec_pop_down);
   signal insn : insn_t;
   type insn_array_t is array(0 to WORD_BYTES-1) of insn_t;
   signal insns : insn_array_t;
   type opcode_array_t is array(0 to WORD_BYTES-1) of unsigned(OPCODE_W-1 downto 0);
   signal opcode_r : opcode_array_t;
begin
   -- the memory subsystem will tell us one cycle later whether or
   -- not it is busy
   write_en_o <= write_en_r;
   read_en_o  <= read_en_r;
   addr_o(ADDR_W-1 downto BYTE_BITS) <= addr_r;
   addr_o(BYTE_BITS-1 downto 0)      <= (others => '0');

   -- SP+1 and +2
   inc_sp     <= sp_r+1;
   inc_inc_sp <= sp_r+2;

   opcode_control:
   process (clk_i)
      variable topcode     : unsigned(OPCODE_W-1 downto 0);
      variable ex_opcode   : unsigned(OPCODE_W-1 downto 0);
      variable sp_offset   : unsigned(4 downto 0);
      variable tsp_offset  : unsigned(4 downto 0);
      variable next_pc     : unsigned(ADDR_W-1 downto 0);
      variable tdecoded    : insn_t;
      variable tinsns   : insn_array_t;
      variable mult_res    : unsigned(WORD_SIZE*2-1 downto 0);
      variable ipc_low     : integer range 0 to 3; -- Address inside a word (pc_r)
      variable inpc_low    : integer range 0 to 3; -- Address inside a word (next_pc)
      variable h_bit       : integer;
      variable l_bit       : integer;
      variable not_lshr    : std_logic:='1';
   begin
      if rising_edge(clk_i) then
         break_o <= '0';
         if reset_i='1' then
            if ENA_IDLE then
               state <= st_idle;
            else
               state <= st_resync;
            end if;
            sp_r         <= SP_START;
            pc_r         <= (others => '0');
            idim_r       <= '0';
            write_en_r   <= '0';
            read_en_r    <= '0';
            mult_a_r     <= (others => '0');
            mult_b_r     <= (others => '0');
            dbg_o.b_inst <= '0';
            -- Reseting add_r here makes XST fail to use BRAMs ?!
         else -- reset_i='1'
            if MULT_PIPE then
               -- We must multiply unconditionally to get pipelined multiplication
               mult_res:=mult_a_r*mult_b_r;
               mult_res1_r <= mult_res(WORD_SIZE-1 downto 0);
               mult_res2_r <= mult_res1_r;
               mult_res3_r <= mult_res2_r;
               mult_a_r    <= (others => D_CARE_VAL);
               mult_b_r    <= (others => D_CARE_VAL);
            end if;

            if BINOP_PIPE=2 then
               bin_op_res2_r <= bin_op_res1_r; -- pipeline a bit.
            end if;
   
            read_en_r  <='0';
            write_en_r <='0';
            -- Allow synthesis tools to load bogus values when we don't
            -- care about the address and output data.
            addr_r     <= (others => D_CARE_VAL);
            data_o     <= (others => D_CARE_VAL);
   
            if (write_en_r='1') and (read_en_r='1') then
               report "read/write collision" severity failure;
            end if;

            ipc_low:=to_integer(pc_r(BYTE_BITS-1 downto 0));
            sp_offset(4):=not opcode_r(ipc_low)(4);
            sp_offset(3 downto 0):=opcode_r(ipc_low)(3 downto 0);
            next_pc:=pc_r+1;
   
            -- Prepare trace snapshot
            dbg_o.opcode <= opcode_r(ipc_low);
            dbg_o.pc     <= resize(pc_r,32);
            dbg_o.stk_a  <= resize(a_r,32);
            dbg_o.stk_b  <= resize(b_r,32);
            dbg_o.b_inst <= '0';
            dbg_o.sp     <= (others => '0');
            dbg_o.sp(ADDR_W-1 downto BYTE_BITS) <= sp_r;
   
            case state is
                 when st_idle =>
                      if enable_i='1' then
                         state <= st_resync;
                      end if;
                 -- Initial state of ZPU, fetch top of stack (A/B) + first instruction
                 when st_resync =>
                      if mem_busy_i='0' then
                         addr_r    <= sp_r;
                         read_en_r <= '1';
                         state     <= st_resync2;
                      end if;
                 when st_resync2 =>
                      if mem_busy_i='0' then
                         a_r       <= data_i;
                         addr_r    <= inc_sp;
                         read_en_r <= '1';
                         state     <= st_resync3;
                      end if;
                 when st_resync3 =>
                      if mem_busy_i='0' then
                         b_r       <= data_i;
                         addr_r    <= pc_r(ADDR_W-1 downto BYTE_BITS);
                         read_en_r <= '1';
                         state     <= st_decode;
                      end if;
                 when st_decode =>
                      if mem_busy_i='0' then
                         -- Here we latch the fetched word to give one full clock
                         -- cycle to the instruction decoder. This could be removed
                         -- if using BRAMs and the decoder delay isn't important.
                         fetched_w_r <= data_i;
                         state       <= st_decode2;
                      end if;
                 when st_decode2 =>
                      -- decode 4 instructions in parallel
                      for i in 0 to WORD_BYTES-1 loop
                          topcode:=fetched_w_r((WORD_BYTES-1-i+1)*8-1 downto (WORD_BYTES-1-i)*8);

                          tsp_offset(4):=not topcode(4);
                          tsp_offset(3 downto 0):=topcode(3 downto 0);

                          opcode_r(i) <= topcode;
                          if topcode(7 downto 7)=OPCODE_IM then
                             tdecoded:=dec_im;
                          elsif topcode(7 downto 5)=OPCODE_STORESP then
                             if tsp_offset=0 then
                                -- Special case, we can avoid a write
                                tdecoded:=dec_pop;
                             elsif tsp_offset=1 then
                                -- Special case, collision
                                tdecoded:=dec_pop_down;
                             else
                                tdecoded:=dec_store_sp;
                             end if;
                          elsif topcode(7 downto 5)=OPCODE_LOADSP then
                             if tsp_offset=0 then
                                tdecoded:=dec_dup;
                             elsif tsp_offset=1 then
                                tdecoded:=dec_dup_stk_b;
                             else
                                tdecoded:=dec_load_sp;
                             end if;
                          elsif topcode(7 downto 5)=OPCODE_EMULATE then
                             tdecoded:=dec_emulate;
                             if ENA_LEVEL0 and topcode(5 downto 0)=OPCODE_NEQBRANCH then
                                tdecoded:=dec_neq_branch;
                             elsif ENA_LEVEL0 and topcode(5 downto 0)=OPCODE_EQ then
                                tdecoded:=dec_eq;
                             elsif ENA_LEVEL0 and topcode(5 downto 0)=OPCODE_LOADB then
                                tdecoded:=dec_loadb;
                             elsif ENA_LEVEL0 and topcode(5 downto 0)=OPCODE_PUSHSPADD then
                                tdecoded:=dec_push_sp_add;
                             elsif ENA_LEVEL1 and topcode(5 downto 0)=OPCODE_LESSTHAN then
                                tdecoded:=dec_less_than;
                             elsif ENA_LEVEL1 and topcode(5 downto 0)=OPCODE_ULESSTHAN then
                                tdecoded:=dec_u_less_than;
                             elsif ENA_LEVEL1 and topcode(5 downto 0)=OPCODE_MULT then
                                tdecoded:=dec_mult;
                             elsif ENA_LEVEL1 and topcode(5 downto 0)=OPCODE_STOREB then
                                tdecoded:=dec_storeb;
                             elsif ENA_LEVEL1 and topcode(5 downto 0)=OPCODE_CALLPCREL then
                                tdecoded:=dec_call_pc_rel;
                             elsif ENA_LEVEL1 and topcode(5 downto 0)=OPCODE_SUB then
                                tdecoded:=dec_sub;
                             elsif ENA_LEVEL2 and topcode(5 downto 0)=OPCODE_LESSTHANOREQUAL then
                                tdecoded:=dec_less_than_or_equal;
                             elsif ENA_LEVEL2 and topcode(5 downto 0)=OPCODE_ULESSTHANOREQUAL then
                                tdecoded:=dec_u_less_than_or_equal;
                             elsif ENA_LEVEL2 and topcode(5 downto 0)=OPCODE_CALL then
                                tdecoded:=dec_call;
                             elsif ENA_LEVEL2 and topcode(5 downto 0)=OPCODE_POPPCREL then
                                tdecoded:=dec_pop_pc_rel;
                             elsif ENA_LSHR and topcode(5 downto 0)=OPCODE_LSHIFTRIGHT then
                                tdecoded:=dec_lshr;
                             end if;
                          elsif topcode(7 downto 4)=OPCODE_ADDSP then
                             if tsp_offset=0 then
                                tdecoded:=dec_shift;
                             elsif tsp_offset=1 then
                                tdecoded:=dec_add_top;
                             else
                                tdecoded:=dec_add_sp;
                             end if;
                          else -- OPCODE_SHORT
                             case topcode(3 downto 0) is
                                  when OPCODE_BREAK =>
                                       tdecoded:=dec_break;
                                  when OPCODE_PUSHSP =>
                                       tdecoded:=dec_push_sp;
                                  when OPCODE_POPPC =>
                                       tdecoded:=dec_pop_pc;
                                  when OPCODE_ADD =>
                                       tdecoded:=dec_add;
                                  when OPCODE_OR =>
                                       tdecoded:=dec_or;
                                  when OPCODE_AND =>
                                       tdecoded:=dec_and;
                                  when OPCODE_LOAD =>
                                       tdecoded:=dec_load;
                                  when OPCODE_NOT =>
                                       tdecoded:=dec_not;
                                  when OPCODE_FLIP =>
                                       tdecoded:=dec_flip;
                                  when OPCODE_STORE =>
                                       tdecoded:=dec_store;
                                  when OPCODE_POPSP =>
                                       tdecoded:=dec_pop_sp;
                                  when others => -- OPCODE_NOP and others
                                       tdecoded:=dec_nop;
                             end case;
                          end if;
                          tinsns(i):=tdecoded;
                      end loop;
                      
                      insn <= tinsns(ipc_low);
                      -- once we wrap, we need to fetch
                      tinsns(0):=dec_insn_fetch;
                      insns <= tinsns;
                      state <= st_execute;

                      -- Each instruction must:
                      --
                      -- 1. increase pc_r if applicable
                      -- 2. set next state if applicable
                      -- 3. do it's operation
                 when st_execute =>
                      -- Some shortcut to make the code readable:
                      inpc_low:=to_integer(next_pc(BYTE_BITS-1 downto 0));
                      ex_opcode:=opcode_r(ipc_low);
                      insn <= insns(inpc_low);
                      -- Defaults used by most instructions
                      if insn/=dec_insn_fetch and insn/=dec_im then
                         dbg_o.b_inst <= '1';
                         idim_r       <= '0';
                      end if;
                      case insn is
                           when dec_insn_fetch =>
                                -- Not a real instruction, fetch new instructions
                                DoFetch(FAST_FETCH,state,addr_r,pc_r,read_en_r,mem_busy_i);
                           when dec_im =>
                                -- Push(immediate value), IDIM=1
                                -- if IDIM=0 Push(signed(opcode & 0x7F)) else
                                --           Push((Pop()<<7)|(opcode&0x7F))
                                if mem_busy_i='0' then
                                   dbg_o.b_inst <= '1';
                                   idim_r       <= '1';
                                   pc_r             <= pc_r+1;
                                   if idim_r='1' then
                                      -- We already started an IM sequence
                                      -- Shift left 7 bits
                                      a_r(WORD_SIZE-1 downto 7) <= a_r(WORD_SIZE-8 downto 0);
                                      -- Put the new value
                                      a_r(6 downto 0) <= ex_opcode(6 downto 0);
                                   else
                                      -- First IM, push the value sign extended
                                      FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                      a_r <= unsigned(resize(signed(ex_opcode(6 downto 0)),WORD_SIZE));
                                      Push(sp_r,a_r,b_r);
                                   end if;
                                end if;
                           when dec_store_sp =>
                                -- [SP+Offset]=Pop()
                                if mem_busy_i='0' then
                                   write_en_r <= '1';
                                   addr_r     <= sp_r+sp_offset;
                                   data_o     <= a_r;
                                   Pop(sp_r,a_r,b_r);
                                   -- We need to fetch B
                                   state      <= st_store_sp2;
                                end if;
                           when dec_load_sp =>
                                -- Push([SP+Offset])
                                if mem_busy_i='0' then
                                   FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                   Push(sp_r,a_r,b_r);
                                   -- We are flushing B cache, so we need more time to
                                   -- read the value.
                                   state <= st_load_sp2;
                                end if;
                           when dec_emulate =>
                                -- Push(PC+1), PC=Opcode[4:0]*32
                                if mem_busy_i='0' then
                                   FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                   state <= st_fetch;
                                   a_r   <= ExpandPC(pc_r+1);
                                   Push(sp_r,a_r,b_r);
                                   -- The emulate address is:
                                   --        98 7654 3210
                                   -- 0000 00aa aaa0 0000
                                   pc_r             <= (others => '0');
                                   pc_r(9 downto 5) <= ex_opcode(4 downto 0);
                                end if;
                           when dec_call_pc_rel =>
                                -- t=Pop(), Push(PC+1), PC=PC+t
                                if mem_busy_i='0' and ENA_LEVEL1 then
                                   state <= st_fetch;
                                   a_r   <= ExpandPC(pc_r+1);
                                   pc_r  <= pc_r+a_r(ADDR_W-1 downto 0);
                                end if;
                           when dec_call =>
                                -- t=Pop(), Push(PC+1), PC=t
                                if mem_busy_i='0' and ENA_LEVEL2 then
                                   state <= st_fetch;
                                   a_r   <= ExpandPC(pc_r+1);
                                   pc_r  <= a_r(ADDR_W-1 downto 0);
                                end if;
                           when dec_add_sp =>
                                -- Push(Pop()+[SP+Offset])
                                if mem_busy_i='0' then
                                   -- Read SP+Offset
                                   state     <= st_add_sp2;
                                   read_en_r <= '1';
                                   addr_r    <= sp_r+sp_offset;
                                   pc_r      <= pc_r+1;
                                end if;
                           when dec_push_sp =>
                                -- Push(SP)
                                if mem_busy_i='0' then
                                   FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                   pc_r <= pc_r+1;
                                   a_r  <= (others => '0');
                                   a_r(ADDR_W-1 downto BYTE_BITS) <= sp_r;
                                   Push(sp_r,a_r,b_r);
                                end if;
                           when dec_pop_pc =>
                                -- PC=Pop() (return)
                                if mem_busy_i='0' then
                                   FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                   state <= st_resync;
                                   pc_r  <= a_r(ADDR_W-1 downto 0);
                                   sp_r  <= inc_sp;
                                end if;
                           when dec_pop_pc_rel =>
                                -- PC=PC+Pop()
                                if mem_busy_i='0' and ENA_LEVEL2 then
                                   FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                   state <= st_resync;
                                   pc_r  <= a_r(ADDR_W-1 downto 0)+pc_r;
                                   sp_r  <= inc_sp;
                                end if;
                           when dec_add =>
                                -- Push(Pop()+Pop())  [A=A+B, SP++, update B]
                                if mem_busy_i='0' then
                                   state     <= st_popped;
                                   a_r       <= a_r+b_r;
                                   read_en_r <= '1';
                                   addr_r    <= inc_inc_sp;
                                   sp_r      <= inc_sp;
                                end if;
                           when dec_sub =>
                                -- a=Pop(), b=Pop(), Push(b-a)
                                if mem_busy_i='0' and ENA_LEVEL1 then
                                   DoBinOp(b_r-a_r,state,sp_r,addr_r,read_en_r,
                                           a_r,bin_op_res1_r,BINOP_PIPE);
                                end if;
                           when dec_pop =>
                                -- Pop()
                                if mem_busy_i='0' then
                                   state     <= st_popped;
                                   addr_r    <= inc_inc_sp;
                                   read_en_r <= '1';
                                   Pop(sp_r,a_r,b_r);
                                end if;
                           when dec_pop_down =>
                                -- t=Pop(), Pop(), Push(t)
                                if mem_busy_i='0' then
                                   -- PopDown leaves top of stack unchanged
                                   state     <= st_popped;
                                   addr_r    <= inc_inc_sp;
                                   read_en_r <= '1';
                                   sp_r      <= inc_sp;
                                end if;
                           when dec_or =>
                                -- Push(Pop() or Pop())
                                if mem_busy_i='0' then
                                   state     <= st_popped;
                                   a_r       <= a_r or b_r;
                                   read_en_r <= '1';
                                   addr_r    <= inc_inc_sp;
                                   sp_r      <= inc_sp;
                                end if;
                           when dec_and =>
                                -- Push(Pop() and Pop())
                                if mem_busy_i='0' then
                                   state     <= st_popped;
                                   a_r       <= a_r and b_r;
                                   read_en_r <= '1';
                                   addr_r    <= inc_inc_sp;
                                   sp_r      <= inc_sp;
                                end if;
                           when dec_eq =>
                                -- a=Pop(), b=Pop(), Push(a=b ? 1 : 0)
                                if mem_busy_i='0' and ENA_LEVEL0 then
                                   DoBinOpBool(a_r=b_r,state,sp_r,addr_r,read_en_r,
                                               a_r,bin_op_res1_r,BINOP_PIPE);
                                end if;
                           when dec_u_less_than =>
                                -- a=Pop(), b=Pop(), Push(a<b ? 1 : 0)
                                if mem_busy_i='0' and ENA_LEVEL1 then
                                   DoBinOpBool(a_r<b_r,state,sp_r,addr_r,read_en_r,
                                               a_r,bin_op_res1_r,BINOP_PIPE);
                                end if;
                           when dec_u_less_than_or_equal =>
                                -- a=Pop(), b=Pop(), Push(a<=b ? 1 : 0)
                                if mem_busy_i='0' and ENA_LEVEL2 then
                                   DoBinOpBool(a_r<=b_r,state,sp_r,addr_r,read_en_r,
                                               a_r,bin_op_res1_r,BINOP_PIPE);
                                end if;
                           when dec_less_than =>
                                -- a=signed(Pop()), b=signed(Pop()), Push(a<b ? 1 : 0)
                                if mem_busy_i='0' and ENA_LEVEL1 then
                                   DoBinOpBool(signed(a_r)<signed(b_r),state,sp_r,
                                               addr_r,read_en_r,a_r,bin_op_res1_r,
                                               BINOP_PIPE);
                                end if;
                           when dec_less_than_or_equal =>
                                -- a=signed(Pop()), b=signed(Pop()), Push(a<=b ? 1 : 0)
                                if mem_busy_i='0' and ENA_LEVEL2 then
                                   DoBinOpBool(signed(a_r)<=signed(b_r),state,sp_r,
                                               addr_r,read_en_r,a_r,bin_op_res1_r,
                                               BINOP_PIPE);
                                end if;
                           when dec_load =>
                                -- Push([Pop()])
                                if mem_busy_i='0' then
                                   state     <= st_load2;
                                   addr_r    <= a_r(ADDR_W-1 downto BYTE_BITS);
                                   read_en_r <= '1';
                                   pc_r      <= pc_r+1;
                                end if;
                           when dec_dup =>
                                -- t=Pop(), Push(t), Push(t)
                                if mem_busy_i='0' then
                                   pc_r <= pc_r+1;
                                   -- A is dupped, no change
                                   Push(sp_r,a_r,b_r);
                                   FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                end if;
                           when dec_dup_stk_b =>
                                -- Pop(), t=Pop(), Push(t), Push(t), Push(t)
                                if mem_busy_i='0' then
                                   pc_r <= pc_r+1;
                                   a_r  <= b_r;
                                   -- B goes to A
                                   Push(sp_r,a_r,b_r);
                                   FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                end if;
                           when dec_store =>
                                -- a=Pop(), b=Pop(), [a]=b
                                if mem_busy_i='0' then
                                   state      <= st_resync;
                                   pc_r       <= pc_r+1;
                                   addr_r     <= a_r(ADDR_W-1 downto BYTE_BITS);
                                   data_o     <= b_r;
                                   write_en_r <= '1';
                                   sp_r       <= inc_inc_sp;
                                end if;
                           when dec_pop_sp =>
                                -- SP=Pop()
                                if mem_busy_i='0' then
                                   FlushB(write_en_r,addr_r,inc_sp,data_o,b_r);
                                   state <= st_resync;
                                   pc_r  <= pc_r+1;
                                   sp_r  <= a_r(ADDR_W-1 downto BYTE_BITS);
                                end if;
                           when dec_nop =>
                                pc_r <= pc_r+1;
                           when dec_not =>
                                -- Push(not(Pop()))
                                pc_r <= pc_r+1;
                                a_r  <= not a_r;
                           when dec_flip =>
                                -- Push(flip(Pop()))
                                pc_r <= pc_r+1;
                                for i in 0 to WORD_SIZE-1 loop
                                    a_r(i) <= a_r(WORD_SIZE-1-i);
                                end loop;
                           when dec_add_top =>
                                -- a=Pop(), b=Pop(), Push(b), Push(a+b)
                                pc_r <= pc_r+1;
                                a_r  <= a_r+b_r;
                           when dec_shift =>
                                -- Push(Pop()<<1) [equivalent to a=Pop(), Push(a+a)]
                                pc_r <= pc_r+1;
                                a_r(WORD_SIZE-1 downto 1) <= a_r(WORD_SIZE-2 downto 0);
                                a_r(0) <= '0';
                           when dec_push_sp_add =>
                                -- Push(Pop()+SP)
                                if ENA_LEVEL0 then
                                   pc_r <= pc_r+1;
                                   a_r  <= (others => '0');
                                   a_r(ADDR_W-1 downto BYTE_BITS) <=
                                      a_r(ADDR_W-1-BYTE_BITS downto 0)+sp_r;
                                end if;
                           when dec_neq_branch =>
                                -- a=Pop(), b=Pop(), PC+=b==0 ? 1 : a
                                -- Branches are almost always taken as they form loops
                                if ENA_LEVEL0 then
                                   sp_r  <= inc_inc_sp;
                                   -- Need to fetch stack again.
                                   state <= st_resync;
                                   if b_r/=0 then
                                      pc_r <= a_r(ADDR_W-1 downto 0)+pc_r;
                                   else
                                      pc_r <= pc_r+1;
                                   end if;
                                end if;
                           when dec_mult =>
                                -- Push(Pop()*Pop())
                                if ENA_LEVEL1 then
                                   if MULT_PIPE then
                                      mult_a_r <= a_r;
                                      mult_b_r <= b_r;
                                      state    <= st_mult2;
                                   else
                                      mult_res:=a_r*b_r;
                                      mult_res1_r <= mult_res(WORD_SIZE-1 downto 0);
                                      state       <= st_mult5;
                                   end if;
                                end if;
                           when dec_break =>
                                -- Assert the break_o signal
                                --report "Break instruction encountered" severity failure;
                                break_o <= '1';
                                pc_r    <= pc_r+1;
                           when dec_loadb =>
                                -- Push([Pop()] & 0xFF) (byte address)
                                if mem_busy_i='0' and ENA_LEVEL0 then
                                   state     <= st_loadb2;
                                   addr_r    <= a_r(ADDR_W-1 downto BYTE_BITS);
                                   read_en_r <= '1';
                                   pc_r      <= pc_r+1;
                                end if;
                           when dec_storeb =>
                                -- [Pop()]=Pop() & 0xFF (byte address)
                                if mem_busy_i='0' and ENA_LEVEL1 then
                                   state     <= st_storeb2;
                                   addr_r    <= a_r(ADDR_W-1 downto BYTE_BITS);
                                   read_en_r <= '1';
                                   pc_r      <= pc_r+1;
                                end if;
                           when dec_lshr =>
                                -- a=Pop(), b=Pop(), Push(b>>(a&0x3F))
                                if ENA_LSHR then
                                   -- This instruction takes more than one cycle.
                                   -- We must avoid duplications in the trace log.
                                   dbg_o.b_inst <= not_lshr;
                                   not_lshr:='0';
                                   if a_r(5 downto 0)=0 then -- Only 6 bits used
                                      -- No more shifts
                                      if mem_busy_i='0' then
                                         state     <= st_popped;
                                         a_r       <= b_r;
                                         read_en_r <= '1';
                                         addr_r    <= inc_inc_sp;
                                         sp_r      <= inc_sp;
                                         not_lshr:='1';
                                      end if;
                                   else -- More shifts needed
                                      b_r <= "0"&b_r(WORD_SIZE-1 downto 1);
                                      a_r(5 downto 0) <= a_r(5 downto 0)-1;
                                      insn <= insn;
                                   end if;
                                end if;
                           when others =>
                                -- Undefined behavior, we shouldn't get here.
                                -- It only helps synthesis tools.
                                sp_r <= (others => D_CARE_VAL);
                                report "Illegal decode instruction?!" severity failure;
                                --break_o <= '1';
                      end case;
                 -- The followup of operations that takes more than one execution clock
                 when st_store_sp2 =>
                      if mem_busy_i='0' then
                         addr_r    <= inc_sp;
                         read_en_r <= '1';
                         state     <= st_popped;
                      end if;
                 when st_load_sp2 =>
                      if mem_busy_i='0' then
                         state     <= st_load_sp3;
                         -- Now we can read SP+Offset (SP already decremented)
                         read_en_r <= '1';
                         addr_r    <= sp_r+sp_offset+1;
                      end if;
                 when st_load_sp3 =>
                      if mem_busy_i='0' then
                         -- Note: We can't increment PC in the decode stage
                         -- because it will modify sp_offset.
                         pc_r  <= pc_r+1;
                         -- Finally we have the result in A
                         state <= st_execute;
                         a_r   <= data_i;
                      end if;
                 when st_add_sp2 =>
                      if mem_busy_i='0' then
                         state <= st_execute;
                         a_r   <= a_r+data_i;
                      end if;
                 when st_load2 =>
                      if mem_busy_i='0' then
                         a_r   <= data_i;
                         state <= st_execute;
                      end if;
                 when st_loadb2 =>
                      if mem_busy_i='0' then
                         a_r <= (others => '0');
                         -- Select the source bits using the less significant bits (byte address)
                         h_bit:=(WORD_BYTES-to_integer(a_r(BYTE_BITS-1 downto 0)))*8-1;
                         l_bit:=h_bit-7;
                         a_r(7 downto 0) <= data_i(h_bit downto l_bit);
                         state <= st_execute;
                      end if;
                 when st_storeb2 =>
                      if mem_busy_i='0' then
                         addr_r <= a_r(ADDR_W-1 downto BYTE_BITS);
                         data_o <= data_i;
                         -- Select the source bits using the less significant bits (byte address)
                         h_bit:=(WORD_BYTES-to_integer(a_r(BYTE_BITS-1 downto 0)))*8-1;
                         l_bit:=h_bit-7;
                         data_o(h_bit downto l_bit) <= b_r(7 downto 0);
                         write_en_r <= '1';
                         sp_r       <= inc_inc_sp;
                         state      <= st_resync;
                      end if;
                 when st_fetch =>
                      if mem_busy_i='0' then
                         addr_r    <= pc_r(ADDR_W-1 downto BYTE_BITS);
                         read_en_r <= '1';
                         state     <= st_decode;
                      end if;
                 -- The following states can be used to leave cycles free for
                 -- tools that can automagically decompose the multiplication
                 -- in various stages. Xilinx tools can do it to increase the
                 -- multipliers performance.
                 when st_mult2 =>
                      state <= st_mult3;
                 when st_mult3 =>
                      state <= st_mult4;
                 when st_mult4 =>
                      state <= st_mult5;
                 when st_mult5 =>
                      if mem_busy_i='0' then
                         if MULT_PIPE then
                            a_r <= mult_res3_r;
                         else
                            a_r <= mult_res1_r;
                         end if;
                         read_en_r <= '1';
                         addr_r    <= inc_inc_sp;
                         sp_r      <= inc_sp;
                         state     <= st_popped;
                      end if;
               when st_binary_op_res =>
                    -- BINOP_PIPE=2
                    state <= st_binary_op_res2;
               when st_binary_op_res2 =>
                    -- BINOP_PIPE>=1
                    read_en_r <= '1';
                    addr_r    <= inc_inc_sp;
                    sp_r      <= inc_sp;
                    state     <= st_popped;
                    if BINOP_PIPE=2 then
                       a_r <= bin_op_res2_r;
                    else -- 1
                       a_r <= bin_op_res1_r;
                    end if;
               when st_popped =>
                    if mem_busy_i='0' then
                       -- Note: Moving this PC++ to the decoder seems to
                       -- consume more LUTs.
                       pc_r  <= pc_r+1;
                       b_r   <= data_i;
                       state <= st_execute;
                    end if;
               when others =>
                    -- Undefined behavior, we shouldn't get here.
                    -- It only helps synthesis tools.
                    sp_r <= (others => D_CARE_VAL);
                    report "Illegal state?!" severity failure;
                    --break_o <= '1';
            end case; -- state
         end if; -- else reset_i='1'
      end if; -- rising_edge(clk_i)
   end process opcode_control;
end architecture Behave; -- Entity: ZPUMediumCore

