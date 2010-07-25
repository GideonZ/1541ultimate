------------------------------------------------------------------------------
----                                                                      ----
----  ZPU Package                                                         ----
----                                                                      ----
----  http://www.opencores.org/                                           ----
----                                                                      ----
----  Description:                                                        ----
----  ZPU is a 32 bits small stack cpu. This is the package.              ----
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
---- Design unit:      zpupkg, UART (Package)                             ----
---- File name:        zpu_medium.vhdl                                    ----
---- Note:             None                                               ----
---- Limitations:      None known                                         ----
---- Errors:           None known                                         ----
---- Library:          zpu                                                ----
---- Dependencies:     IEEE.std_logic_1164                                ----
----                   IEEE.numeric_std                                   ----
---- Target FPGA:      Spartan 3 (XC3S400-4-FT256)                        ----
---- Language:         VHDL                                               ----
---- Wishbone:         No                                                 ----
---- Synthesis tools:  Xilinx Release 9.2.03i - xst J.39                  ----
---- Simulation tools: GHDL [Sokcho edition] (0.2x)                       ----
---- Text editor:      SETEdit 0.5.x                                      ----
----                                                                      ----
------------------------------------------------------------------------------

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

package zpupkg is
   constant c_opcode_width : integer:=8;

   -- Debug structure, currently only for the trace module
   type zpu_dbgo_t is record
      b_inst : std_logic;
      opcode : unsigned(c_opcode_width-1 downto 0);
      pc     : unsigned(31 downto 0);
      sp     : unsigned(31 downto 0);
      stk_a  : unsigned(31 downto 0);
      stk_b  : unsigned(31 downto 0);
   end record;

   component Trace is
      generic(
         LOG_FILE   : string:="trace.txt"; -- Name of the trace file
         ADDR_W     : integer:=16;  -- Address width
         WORD_SIZE  : integer:=32); -- 16/32
      port(
         clk_i      : in std_logic;
         dbg_i      : in zpu_dbgo_t;
         stop_i     : in std_logic;
         busy_i     : in std_logic
         );
   end component Trace;

   component ZPUSmallCore is
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
   end component ZPUSmallCore;

   component ZPUMediumCore is
      generic(
         WORD_SIZE    : integer:=32;  -- Data width 16/32
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
   end component ZPUMediumCore;

   component Timer is
      port(
         clk_i    : in  std_logic;
         reset_i  : in  std_logic;
         we_i     : in  std_logic;
         data_i   : in  unsigned(31 downto 0);
         addr_i   : in  unsigned(0 downto 0);
         data_o   : out unsigned(31 downto 0));
   end component Timer;

   component ZPUPhiIO is
      generic(
         BRDIVISOR : positive:=1;   -- Baud rate divisor i.e. br_clk/9600/4
         ENA_LOG   : boolean:=true; -- Enable log
         LOG_FILE  : string:="log.txt"); -- Name for the log file
      port(
         clk_i      : in  std_logic; -- System Clock
         reset_i    : in  std_logic; -- Synchronous Reset
         busy_o     : out std_logic; -- I/O is busy
         we_i       : in  std_logic; -- Write Enable
         re_i       : in  std_logic; -- Read Enable
         data_i     : in  unsigned(31 downto 0);
         data_o     : out unsigned(31 downto 0);
         addr_i     : in  unsigned(2 downto 0); -- Address bits 4-2
         rs232_rx_i : in  std_logic;  -- UART Rx input
         rs232_tx_o : out std_logic;  -- UART Tx output
         br_clk_i   : in  std_logic); -- UART base clock (enable)
   end component ZPUPhiIO;

   -- Opcode decode constants
   -- Note: these are the basic opcodes, always implemented using hardware.
   constant OPCODE_IM        : unsigned(7 downto 7):="1";
   constant OPCODE_STORESP   : unsigned(7 downto 5):="010";
   constant OPCODE_LOADSP    : unsigned(7 downto 5):="011";
   constant OPCODE_EMULATE   : unsigned(7 downto 5):="001";
   constant OPCODE_ADDSP     : unsigned(7 downto 4):="0001";
   constant OPCODE_SHORT     : unsigned(7 downto 4):="0000";
   
   constant OPCODE_BREAK     : unsigned(3 downto 0):="0000";
   constant OPCODE_SHIFTLEFT : unsigned(3 downto 0):="0001";
   constant OPCODE_PUSHSP    : unsigned(3 downto 0):="0010";
   constant OPCODE_PUSHINT   : unsigned(3 downto 0):="0011";
   
   constant OPCODE_POPPC     : unsigned(3 downto 0):="0100";
   constant OPCODE_ADD       : unsigned(3 downto 0):="0101";
   constant OPCODE_AND       : unsigned(3 downto 0):="0110";
   constant OPCODE_OR        : unsigned(3 downto 0):="0111";
   
   constant OPCODE_LOAD      : unsigned(3 downto 0):="1000";
   constant OPCODE_NOT       : unsigned(3 downto 0):="1001";
   constant OPCODE_FLIP      : unsigned(3 downto 0):="1010";
   constant OPCODE_NOP       : unsigned(3 downto 0):="1011";
   
   constant OPCODE_STORE     : unsigned(3 downto 0):="1100";
   constant OPCODE_POPSP     : unsigned(3 downto 0):="1101";
   constant OPCODE_COMPARE   : unsigned(3 downto 0):="1110";
   constant OPCODE_POPINT    : unsigned(3 downto 0):="1111";

   -- The following instructions are emulated in the small version and
   -- implemented as hardware in the full version.
   -- The constants correpond to the "emulated" instruction number.

   -- Enabled by the ENA_LEVEL0 generic:
   constant OPCODE_EQ               : unsigned(5 downto 0):=to_unsigned(46,6);
   constant OPCODE_LOADB            : unsigned(5 downto 0):=to_unsigned(51,6);
   constant OPCODE_NEQBRANCH        : unsigned(5 downto 0):=to_unsigned(56,6);
   constant OPCODE_PUSHSPADD        : unsigned(5 downto 0):=to_unsigned(61,6);
   -- Enabled by the ENA_LEVEL1 generic:
   constant OPCODE_LESSTHAN         : unsigned(5 downto 0):=to_unsigned(36,6);
   constant OPCODE_ULESSTHAN        : unsigned(5 downto 0):=to_unsigned(38,6);
   constant OPCODE_MULT             : unsigned(5 downto 0):=to_unsigned(41,6);
   constant OPCODE_STOREB           : unsigned(5 downto 0):=to_unsigned(52,6);
   constant OPCODE_CALLPCREL        : unsigned(5 downto 0):=to_unsigned(63,6);
   constant OPCODE_SUB              : unsigned(5 downto 0):=to_unsigned(49,6);
   -- Enabled by the ENA_LEVEL2 generic:
   constant OPCODE_LESSTHANOREQUAL  : unsigned(5 downto 0):=to_unsigned(37,6);
   constant OPCODE_ULESSTHANOREQUAL : unsigned(5 downto 0):=to_unsigned(39,6);
   constant OPCODE_CALL             : unsigned(5 downto 0):=to_unsigned(45,6);
   constant OPCODE_POPPCREL         : unsigned(5 downto 0):=to_unsigned(57,6);
   -- Enabled by the ENA_LSHR generic:
   constant OPCODE_LSHIFTRIGHT      : unsigned(5 downto 0):=to_unsigned(42,6);
   -- The following opcodes are always emulated.
   constant OPCODE_LOADH            : unsigned(5 downto 0):=to_unsigned(34,6);
   constant OPCODE_STOREH           : unsigned(5 downto 0):=to_unsigned(35,6);
   constant OPCODE_ASHIFTLEFT       : unsigned(5 downto 0):=to_unsigned(43,6);
   constant OPCODE_ASHIFTRIGHT      : unsigned(5 downto 0):=to_unsigned(44,6);
   constant OPCODE_NEQ              : unsigned(5 downto 0):=to_unsigned(47,6);
   constant OPCODE_NEG              : unsigned(5 downto 0):=to_unsigned(48,6);
   constant OPCODE_XOR              : unsigned(5 downto 0):=to_unsigned(50,6);
   constant OPCODE_DIV              : unsigned(5 downto 0):=to_unsigned(53,6);
   constant OPCODE_MOD              : unsigned(5 downto 0):=to_unsigned(54,6);
   constant OPCODE_EQBRANCH         : unsigned(5 downto 0):=to_unsigned(55,6);
   constant OPCODE_CONFIG           : unsigned(5 downto 0):=to_unsigned(58,6);
   constant OPCODE_PUSHPC           : unsigned(5 downto 0):=to_unsigned(59,6);
end package zpupkg;

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

package UART is
   ----------------------
   -- Very simple UART --
   ----------------------
   component RxUnit is
      port(
         clk_i    : in  std_logic;  -- System clock signal
         reset_i  : in  std_logic;  -- Reset input (sync)
         enable_i : in  std_logic;  -- Enable input (rate*4)
         read_i   : in  std_logic;  -- Received Byte Read
         rxd_i    : in  std_logic;  -- RS-232 data input
         rxav_o   : out std_logic;  -- Byte available
         datao_o  : out std_logic_vector(7 downto 0)); -- Byte received
   end component RxUnit;

   component TxUnit is
     port (
        clk_i    : in  std_logic;  -- Clock signal
        reset_i  : in  std_logic;  -- Reset input
        enable_i : in  std_logic;  -- Enable input
        load_i   : in  std_logic;  -- Load input
        txd_o    : out std_logic;  -- RS-232 data output
        busy_o   : out std_logic;  -- Tx Busy
        datai_i  : in  std_logic_vector(7 downto 0)); -- Byte to transmit
   end component TxUnit;

   component BRGen is
     generic(
        COUNT : integer range 0 to 65535);-- Count revolution
     port (
        clk_i   : in  std_logic;  -- Clock
        reset_i : in  std_logic;  -- Reset input
        ce_i    : in  std_logic;  -- Chip Enable
        o_o     : out std_logic); -- Output
   end component BRGen;
end package UART;

