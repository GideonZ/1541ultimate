library ecp5u;
use ecp5u.components.all;
library ieee;
use ieee.std_logic_1164.all;

entity sync_fifo_uniq_3_7_9_4_MRAM_block_ram_true is
port(
command : in std_logic_vector(7 downto 0);
command_fifo_dout : out std_logic_vector(8 downto 0);
completion :  in std_logic;
sys_clock :  in std_logic;
command_fifo_push :  in std_logic;
command_fifo_pop :  in std_logic;
drv_reset :  in std_logic;
reveal_ist_452 :  out std_logic);
end sync_fifo_uniq_3_7_9_4_MRAM_block_ram_true;

architecture beh of sync_fifo_uniq_3_7_9_4_MRAM_block_ram_true is
signal RD_PNT : std_logic_vector(2 downto 0);
signal NUM_EL : std_logic_vector(2 downto 0);
signal WR_PNT_RNO : std_logic_vector(2 downto 0);
signal WR_PNT : std_logic_vector(2 downto 0);
signal WR_PNT_QN_4 : std_logic_vector(2 downto 0);
signal RD_PNT_QN_4 : std_logic_vector(2 downto 0);
signal NUM_EL_QN_4 : std_logic_vector(2 downto 0);
signal DATA_ARRAY : std_logic_vector(8 downto 0);
signal DOUT_QN_4 : std_logic_vector(7 downto 0);
signal DOUT_QN_3 : std_logic_vector(8 to 8);
signal DATA_ARRAY_WADDR_TMP : std_logic_vector(2 downto 0);
signal DATA_ARRAY_WADDR_TMP_QN_1 : std_logic_vector(2 downto 0);
signal DATA_ARRAY_DIN_TMP : std_logic_vector(8 downto 0);
signal DATA_ARRAY_DIN_TMP_QN_1 : std_logic_vector(8 downto 0);
signal RD_PNT_0_0 : std_logic_vector(0 to 0);
signal RD_PNT_0 : std_logic_vector(2 downto 0);
signal DATA_ARRAY_DOUT_TMP : std_logic_vector(8 downto 0);
signal REVEAL_IST_1032 : std_logic ;
signal EMPTY_I : std_logic ;
signal VALID_I_RNO_0 : std_logic ;
signal RD_ENABLE : std_logic ;
signal RD_PNTC : std_logic ;
signal RD_PNTC_0 : std_logic ;
signal FULL_I : std_logic ;
signal SUM0 : std_logic ;
signal VCC : std_logic ;
signal VALID_I_QN_4 : std_logic ;
signal RD_PNTC_1 : std_logic ;
signal SUM1 : std_logic ;
signal SUM2 : std_logic ;
signal FULL_I_R : std_logic ;
signal FULL_I_QN_6 : std_logic ;
signal EMPTY_I_S : std_logic ;
signal EMPTY_I_QN_4 : std_logic ;
signal UN1_WR_EN_FLT_0 : std_logic ;
signal DATA_ARRAY_G_1_Q : std_logic ;
signal DATA_ARRAY_G_1_QN_1 : std_logic ;
signal CO1 : std_logic ;
signal N_7 : std_logic ;
signal UN9_RD_PNT_NEXT_0_SQMUXA : std_logic ;
signal DATA_ARRAY_N_3 : std_logic ;
signal DATA_ARRAY_G_2_1 : std_logic ;
signal GND : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO0 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO1 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO2 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO3 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO4 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO5 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO6 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO7 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO8 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO9 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO10 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO11 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO12 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO13 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO14 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO15 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO16 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO17 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO27 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO28 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO29 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO30 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO31 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO32 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO33 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO34 : std_logic ;
signal DATA_ARRAY_DATA_ARRAY_0_0_DO35 : std_logic ;
begin
VALID_I_RNO: LUT4 
generic map(
  init => X"0323"
)
port map (
A => REVEAL_IST_1032,
B => drv_reset,
C => EMPTY_I,
D => command_fifo_pop,
Z => VALID_I_RNO_0);
\RD_PNT_RNO[0]\: LUT4 
generic map(
  init => X"2666"
)
port map (
A => RD_PNT(0),
B => RD_ENABLE,
C => RD_PNT(2),
D => RD_PNT(1),
Z => RD_PNTC);
\RD_PNT_RNO[1]\: LUT4 
generic map(
  init => X"4A6A"
)
port map (
A => RD_PNT(1),
B => RD_PNT(0),
C => RD_ENABLE,
D => RD_PNT(2),
Z => RD_PNTC_0);
\UN1_NUM_EL_1_1.SUM0\: LUT4 
generic map(
  init => X"6966"
)
port map (
A => RD_ENABLE,
B => NUM_EL(0),
C => FULL_I,
D => command_fifo_push,
Z => SUM0);
\WR_PNT[0]_REG_Z176\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => WR_PNT_RNO(0),
CK => sys_clock,
CD => drv_reset,
Q => WR_PNT(0));
\WR_PNT[1]_REG_Z178\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => WR_PNT_RNO(1),
CK => sys_clock,
CD => drv_reset,
Q => WR_PNT(1));
\WR_PNT[2]_REG_Z180\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => WR_PNT_RNO(2),
CK => sys_clock,
CD => drv_reset,
Q => WR_PNT(2));
VALID_I_REG_Z182: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => VALID_I_RNO_0,
CK => sys_clock,
Q => REVEAL_IST_1032);
\RD_PNT[0]_REG_Z184\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => RD_PNTC,
CK => sys_clock,
CD => drv_reset,
Q => RD_PNT(0));
\RD_PNT[1]_REG_Z186\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => RD_PNTC_0,
CK => sys_clock,
CD => drv_reset,
Q => RD_PNT(1));
\RD_PNT[2]_REG_Z188\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => RD_PNTC_1,
CK => sys_clock,
CD => drv_reset,
Q => RD_PNT(2));
\NUM_EL[0]_REG_Z190\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => SUM0,
CK => sys_clock,
CD => drv_reset,
Q => NUM_EL(0));
\NUM_EL[1]_REG_Z192\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => SUM1,
CK => sys_clock,
CD => drv_reset,
Q => NUM_EL(1));
\NUM_EL[2]_REG_Z194\: FD1S3IX 
generic map(
  GSR => "DISABLED"
)
port map (
D => SUM2,
CK => sys_clock,
CD => drv_reset,
Q => NUM_EL(2));
FULL_I_REG_Z196: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => FULL_I_R,
CK => sys_clock,
Q => FULL_I);
EMPTY_I_REG_Z198: FD1S3AY 
generic map(
  GSR => "DISABLED"
)
port map (
D => EMPTY_I_S,
CK => sys_clock,
Q => EMPTY_I);
\DOUT[0]_REG_Z200\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(0),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(0));
\DOUT[1]_REG_Z202\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(1),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(1));
\DOUT[2]_REG_Z204\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(2),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(2));
\DOUT[3]_REG_Z206\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(3),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(3));
\DOUT[4]_REG_Z208\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(4),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(4));
\DOUT[5]_REG_Z210\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(5),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(5));
\DOUT[6]_REG_Z212\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(6),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(6));
\DOUT[7]_REG_Z214\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(7),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(7));
\DOUT[8]_REG_Z216\: FD1P3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => DATA_ARRAY(8),
SP => RD_ENABLE,
CK => sys_clock,
Q => command_fifo_dout(8));
\DATA_ARRAY_WADDR_TMP[0]_REG_Z218\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => WR_PNT(0),
CK => sys_clock,
Q => DATA_ARRAY_WADDR_TMP(0));
\DATA_ARRAY_WADDR_TMP[1]_REG_Z220\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => WR_PNT(1),
CK => sys_clock,
Q => DATA_ARRAY_WADDR_TMP(1));
\DATA_ARRAY_WADDR_TMP[2]_REG_Z222\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => WR_PNT(2),
CK => sys_clock,
Q => DATA_ARRAY_WADDR_TMP(2));
\DATA_ARRAY_DIN_TMP[0]_REG_Z224\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => command(0),
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(0));
\DATA_ARRAY_DIN_TMP[1]_REG_Z226\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => command(1),
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(1));
\DATA_ARRAY_DIN_TMP[2]_REG_Z228\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => command(2),
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(2));
\DATA_ARRAY_DIN_TMP[3]_REG_Z230\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => command(3),
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(3));
\DATA_ARRAY_DIN_TMP[4]_REG_Z232\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => command(4),
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(4));
\DATA_ARRAY_DIN_TMP[5]_REG_Z234\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => command(5),
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(5));
\DATA_ARRAY_DIN_TMP[6]_REG_Z236\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => command(6),
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(6));
\DATA_ARRAY_DIN_TMP[7]_REG_Z238\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => command(7),
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(7));
\DATA_ARRAY_DIN_TMP[8]_REG_Z240\: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => completion,
CK => sys_clock,
Q => DATA_ARRAY_DIN_TMP(8));
DATA_ARRAY_G_1_REG_Z242: FD1S3AX 
generic map(
  GSR => "DISABLED"
)
port map (
D => UN1_WR_EN_FLT_0,
CK => sys_clock,
Q => DATA_ARRAY_G_1_Q);
EMPTY_I_S_Z243: LUT4 
generic map(
  init => X"FF01"
)
port map (
A => SUM0,
B => SUM1,
C => SUM2,
D => drv_reset,
Z => EMPTY_I_S);
FULL_I_R_Z244: LUT4 
generic map(
  init => X"0080"
)
port map (
A => SUM0,
B => SUM1,
C => SUM2,
D => drv_reset,
Z => FULL_I_R);
\UN1_NUM_EL_1_1.SUM2\: LUT4 
generic map(
  init => X"639C"
)
port map (
A => UN1_WR_EN_FLT_0,
B => NUM_EL(2),
C => RD_ENABLE,
D => CO1,
Z => SUM2);
\UN1_NUM_EL_1_1.CO1\: LUT4 
generic map(
  init => X"5480"
)
port map (
A => UN1_WR_EN_FLT_0,
B => NUM_EL(0),
C => NUM_EL(1),
D => RD_ENABLE,
Z => CO1);
RD_PNTC_1_Z247: LUT4 
generic map(
  init => X"2222"
)
port map (
A => N_7,
B => UN9_RD_PNT_NEXT_0_SQMUXA,
C => VCC,
D => VCC,
Z => RD_PNTC_1);
\UN1_NUM_EL_1_1.SUM1\: LUT4 
generic map(
  init => X"E178"
)
port map (
A => UN1_WR_EN_FLT_0,
B => NUM_EL(0),
C => NUM_EL(1),
D => RD_ENABLE,
Z => SUM1);
\RD_PNT_RNISLKC[2]\: LUT4 
generic map(
  init => X"7F80"
)
port map (
A => RD_ENABLE,
B => RD_PNT(0),
C => RD_PNT(1),
D => RD_PNT(2),
Z => N_7);
\WR_PNT_RNO[2]_Z250\: LUT4 
generic map(
  init => X"5F80"
)
port map (
A => UN1_WR_EN_FLT_0,
B => WR_PNT(0),
C => WR_PNT(1),
D => WR_PNT(2),
Z => WR_PNT_RNO(2));
\RD_PNT_0[1]_Z251\: LUT4 
generic map(
  init => X"7800"
)
port map (
A => RD_ENABLE,
B => RD_PNT(0),
C => RD_PNT(1),
D => RD_PNT_0_0(0),
Z => RD_PNT_0(1));
\DATA_ARRAY_DOUT[5]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(5),
C => DATA_ARRAY_DOUT_TMP(5),
D => VCC,
Z => DATA_ARRAY(5));
\DATA_ARRAY_DOUT[3]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(3),
C => DATA_ARRAY_DOUT_TMP(3),
D => VCC,
Z => DATA_ARRAY(3));
\DATA_ARRAY_DOUT[7]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(7),
C => DATA_ARRAY_DOUT_TMP(7),
D => VCC,
Z => DATA_ARRAY(7));
\DATA_ARRAY_DOUT[4]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(4),
C => DATA_ARRAY_DOUT_TMP(4),
D => VCC,
Z => DATA_ARRAY(4));
\DATA_ARRAY_DOUT[6]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(6),
C => DATA_ARRAY_DOUT_TMP(6),
D => VCC,
Z => DATA_ARRAY(6));
\DATA_ARRAY_DOUT[8]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(8),
C => DATA_ARRAY_DOUT_TMP(8),
D => VCC,
Z => DATA_ARRAY(8));
\DATA_ARRAY_DOUT[2]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(2),
C => DATA_ARRAY_DOUT_TMP(2),
D => VCC,
Z => DATA_ARRAY(2));
\DATA_ARRAY_DOUT[1]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(1),
C => DATA_ARRAY_DOUT_TMP(1),
D => VCC,
Z => DATA_ARRAY(1));
\DATA_ARRAY_DOUT[0]\: LUT4 
generic map(
  init => X"D8D8"
)
port map (
A => DATA_ARRAY_N_3,
B => DATA_ARRAY_DIN_TMP(0),
C => DATA_ARRAY_DOUT_TMP(0),
D => VCC,
Z => DATA_ARRAY(0));
\WR_PNT_RNO[0]_Z261\: LUT4 
generic map(
  init => X"4666"
)
port map (
A => UN1_WR_EN_FLT_0,
B => WR_PNT(0),
C => WR_PNT(1),
D => WR_PNT(2),
Z => WR_PNT_RNO(0));
\WR_PNT_RNO[1]_Z262\: LUT4 
generic map(
  init => X"5878"
)
port map (
A => UN1_WR_EN_FLT_0,
B => WR_PNT(0),
C => WR_PNT(1),
D => WR_PNT(2),
Z => WR_PNT_RNO(1));
\RD_PNT_0_0[0]_Z263\: LUT4 
generic map(
  init => X"1111"
)
port map (
A => UN9_RD_PNT_NEXT_0_SQMUXA,
B => drv_reset,
C => VCC,
D => VCC,
Z => RD_PNT_0_0(0));
UN9_RD_PNT_NEXT_0_SQMUXA_Z264: LUT4 
generic map(
  init => X"2000"
)
port map (
A => RD_ENABLE,
B => RD_PNT(0),
C => RD_PNT(1),
D => RD_PNT(2),
Z => UN9_RD_PNT_NEXT_0_SQMUXA);
DATA_ARRAY_G_2: LUT4 
generic map(
  init => X"8008"
)
port map (
A => DATA_ARRAY_G_2_1,
B => DATA_ARRAY_G_1_Q,
C => DATA_ARRAY_WADDR_TMP(2),
D => RD_PNT(2),
Z => DATA_ARRAY_N_3);
RD_ENABLE_Z266: LUT4 
generic map(
  init => X"2323"
)
port map (
A => command_fifo_pop,
B => EMPTY_I,
C => REVEAL_IST_1032,
D => VCC,
Z => RD_ENABLE);
DATA_ARRAY_G_2_1_Z267: LUT4 
generic map(
  init => X"8421"
)
port map (
A => DATA_ARRAY_WADDR_TMP(0),
B => DATA_ARRAY_WADDR_TMP(1),
C => RD_PNT(0),
D => RD_PNT(1),
Z => DATA_ARRAY_G_2_1);
UN1_WR_EN_FLT: LUT4 
generic map(
  init => X"2222"
)
port map (
A => command_fifo_push,
B => FULL_I,
C => VCC,
D => VCC,
Z => UN1_WR_EN_FLT_0);
\RD_PNT_0[0]_Z269\: LUT4 
generic map(
  init => X"0006"
)
port map (
A => RD_PNT(0),
B => RD_ENABLE,
C => drv_reset,
D => UN9_RD_PNT_NEXT_0_SQMUXA,
Z => RD_PNT_0(0));
\RD_PNT_0[2]_Z270\: LUT4 
generic map(
  init => X"0202"
)
port map (
A => N_7,
B => drv_reset,
C => UN9_RD_PNT_NEXT_0_SQMUXA,
D => VCC,
Z => RD_PNT_0(2));
DATA_ARRAY_DATA_ARRAY_0_0: PDPW16KD 
generic map(
  DATA_WIDTH_R => 36
)
port map (
DI0 => command(0),
DI1 => command(1),
DI2 => command(2),
DI3 => command(3),
DI4 => command(4),
DI5 => command(5),
DI6 => command(6),
DI7 => command(7),
DI8 => completion,
DI9 => GND,
DI10 => GND,
DI11 => GND,
DI12 => GND,
DI13 => GND,
DI14 => GND,
DI15 => GND,
DI16 => GND,
DI17 => GND,
DI18 => GND,
DI19 => GND,
DI20 => GND,
DI21 => GND,
DI22 => GND,
DI23 => GND,
DI24 => GND,
DI25 => GND,
DI26 => GND,
DI27 => GND,
DI28 => GND,
DI29 => GND,
DI30 => GND,
DI31 => GND,
DI32 => GND,
DI33 => GND,
DI34 => GND,
DI35 => GND,
ADW0 => WR_PNT(0),
ADW1 => WR_PNT(1),
ADW2 => WR_PNT(2),
ADW3 => GND,
ADW4 => GND,
ADW5 => GND,
ADW6 => GND,
ADW7 => GND,
ADW8 => GND,
BE0 => VCC,
BE1 => VCC,
BE2 => VCC,
BE3 => VCC,
CEW => UN1_WR_EN_FLT_0,
CLKW => sys_clock,
CSW0 => GND,
CSW1 => GND,
CSW2 => GND,
ADR0 => GND,
ADR1 => GND,
ADR2 => GND,
ADR3 => GND,
ADR4 => GND,
ADR5 => RD_PNT_0(0),
ADR6 => RD_PNT_0(1),
ADR7 => RD_PNT_0(2),
ADR8 => GND,
ADR9 => GND,
ADR10 => GND,
ADR11 => GND,
ADR12 => GND,
ADR13 => GND,
CER => VCC,
CLKR => sys_clock,
CSR0 => GND,
CSR1 => GND,
CSR2 => GND,
RST => GND,
OCER => VCC,
DO0 => DATA_ARRAY_DATA_ARRAY_0_0_DO0,
DO1 => DATA_ARRAY_DATA_ARRAY_0_0_DO1,
DO2 => DATA_ARRAY_DATA_ARRAY_0_0_DO2,
DO3 => DATA_ARRAY_DATA_ARRAY_0_0_DO3,
DO4 => DATA_ARRAY_DATA_ARRAY_0_0_DO4,
DO5 => DATA_ARRAY_DATA_ARRAY_0_0_DO5,
DO6 => DATA_ARRAY_DATA_ARRAY_0_0_DO6,
DO7 => DATA_ARRAY_DATA_ARRAY_0_0_DO7,
DO8 => DATA_ARRAY_DATA_ARRAY_0_0_DO8,
DO9 => DATA_ARRAY_DATA_ARRAY_0_0_DO9,
DO10 => DATA_ARRAY_DATA_ARRAY_0_0_DO10,
DO11 => DATA_ARRAY_DATA_ARRAY_0_0_DO11,
DO12 => DATA_ARRAY_DATA_ARRAY_0_0_DO12,
DO13 => DATA_ARRAY_DATA_ARRAY_0_0_DO13,
DO14 => DATA_ARRAY_DATA_ARRAY_0_0_DO14,
DO15 => DATA_ARRAY_DATA_ARRAY_0_0_DO15,
DO16 => DATA_ARRAY_DATA_ARRAY_0_0_DO16,
DO17 => DATA_ARRAY_DATA_ARRAY_0_0_DO17,
DO18 => DATA_ARRAY_DOUT_TMP(0),
DO19 => DATA_ARRAY_DOUT_TMP(1),
DO20 => DATA_ARRAY_DOUT_TMP(2),
DO21 => DATA_ARRAY_DOUT_TMP(3),
DO22 => DATA_ARRAY_DOUT_TMP(4),
DO23 => DATA_ARRAY_DOUT_TMP(5),
DO24 => DATA_ARRAY_DOUT_TMP(6),
DO25 => DATA_ARRAY_DOUT_TMP(7),
DO26 => DATA_ARRAY_DOUT_TMP(8),
DO27 => DATA_ARRAY_DATA_ARRAY_0_0_DO27,
DO28 => DATA_ARRAY_DATA_ARRAY_0_0_DO28,
DO29 => DATA_ARRAY_DATA_ARRAY_0_0_DO29,
DO30 => DATA_ARRAY_DATA_ARRAY_0_0_DO30,
DO31 => DATA_ARRAY_DATA_ARRAY_0_0_DO31,
DO32 => DATA_ARRAY_DATA_ARRAY_0_0_DO32,
DO33 => DATA_ARRAY_DATA_ARRAY_0_0_DO33,
DO34 => DATA_ARRAY_DATA_ARRAY_0_0_DO34,
DO35 => DATA_ARRAY_DATA_ARRAY_0_0_DO35);
II_VCC: VHI port map (
Z => VCC);
II_GND: VLO port map (
Z => GND);
reveal_ist_452 <= REVEAL_IST_1032;
end beh;
