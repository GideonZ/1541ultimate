--Legal Notice: (C)2015 Altera Corporation. All rights reserved.  Your
--use of Altera Corporation's design tools, logic functions and other
--software and tools, and its AMPP partner logic functions, and any
--output files any of the foregoing (including device programming or
--simulation files), and any associated documentation or information are
--expressly subject to the terms and conditions of the Altera Program
--License Subscription Agreement or other applicable license agreement,
--including, without limitation, that your use is for the sole purpose
--of programming logic devices manufactured by Altera and sold by Altera
--or its authorized distributors.  Please refer to the applicable
--agreement for further details.


-- turn off superfluous VHDL processor warnings 
-- altera message_level Level1 
-- altera message_off 10034 10035 10036 10037 10230 10240 10030 

library altera;
use altera.altera_europa_support_lib.all;

library altera_mf;
use altera_mf.altera_mf_components.all;

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity ddr2_example_driver is 
        port (
              -- inputs:
                 signal clk : IN STD_LOGIC;
                 signal local_rdata : IN STD_LOGIC_VECTOR (15 DOWNTO 0);
                 signal local_rdata_valid : IN STD_LOGIC;
                 signal local_ready : IN STD_LOGIC;
                 signal reset_n : IN STD_LOGIC;

              -- outputs:
                 signal local_autopch_req : OUT STD_LOGIC;
                 signal local_bank_addr : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
                 signal local_be : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
                 signal local_burstbegin : OUT STD_LOGIC;
                 signal local_col_addr : OUT STD_LOGIC_VECTOR (9 DOWNTO 0);
                 signal local_cs_addr : OUT STD_LOGIC;
                 signal local_read_req : OUT STD_LOGIC;
                 signal local_row_addr : OUT STD_LOGIC_VECTOR (13 DOWNTO 0);
                 signal local_size : OUT STD_LOGIC_VECTOR (2 DOWNTO 0);
                 signal local_wdata : OUT STD_LOGIC_VECTOR (15 DOWNTO 0);
                 signal local_write_req : OUT STD_LOGIC;
                 signal pnf_per_byte : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
                 signal pnf_persist : OUT STD_LOGIC;
                 signal test_complete : OUT STD_LOGIC;
                 signal test_status : OUT STD_LOGIC_VECTOR (7 DOWNTO 0)
              );
attribute ALTERA_ATTRIBUTE : string;
attribute ALTERA_ATTRIBUTE of ddr2_example_driver : entity is "MESSAGE_DISABLE=14130;MESSAGE_DISABLE=14110";
end entity ddr2_example_driver;


architecture europa of ddr2_example_driver is
  component ddr2_ex_lfsr8 is
GENERIC (
      seed : NATURAL
      );
    PORT (
    signal data : OUT STD_LOGIC_VECTOR (7 DOWNTO 0);
        signal pause : IN STD_LOGIC;
        signal enable : IN STD_LOGIC;
        signal clk : IN STD_LOGIC;
        signal reset_n : IN STD_LOGIC;
        signal ldata : IN STD_LOGIC_VECTOR (7 DOWNTO 0);
        signal load : IN STD_LOGIC
      );
  end component ddr2_ex_lfsr8;
                signal COUNTER_VALUE :  STD_LOGIC_VECTOR (19 DOWNTO 0);
                signal LOCAL_BURST_LEN_s :  STD_LOGIC_VECTOR (2 DOWNTO 0);
                signal MAX_BANK :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal MAX_CHIPSEL :  STD_LOGIC;
                signal MAX_COL :  STD_LOGIC_VECTOR (9 DOWNTO 0);
                signal MAX_ROW :  STD_LOGIC_VECTOR (13 DOWNTO 0);
                signal MAX_ROW_PIN :  STD_LOGIC_VECTOR (13 DOWNTO 0);
                signal MIN_CHIPSEL :  STD_LOGIC;
                signal addr_value :  STD_LOGIC_VECTOR (4 DOWNTO 0);
                signal autopch_req :  STD_LOGIC;
                signal avalon_burst_mode :  STD_LOGIC;
                signal bank_addr :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal be :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal burst_beat_count :  STD_LOGIC_VECTOR (2 DOWNTO 0);
                signal burst_begin :  STD_LOGIC;
                signal col_addr :  STD_LOGIC_VECTOR (9 DOWNTO 0);
                signal compare :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal compare_reg :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal compare_valid :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal compare_valid_reg :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal cs_addr :  STD_LOGIC;
                signal dgen_data :  STD_LOGIC_VECTOR (15 DOWNTO 0);
                signal dgen_enable :  STD_LOGIC;
                signal dgen_ldata :  STD_LOGIC_VECTOR (15 DOWNTO 0);
                signal dgen_load :  STD_LOGIC;
                signal dgen_pause :  STD_LOGIC;
                signal enable_be :  STD_LOGIC;
                signal full_burst_on :  STD_LOGIC;
                signal internal_test_complete :  STD_LOGIC;
                signal last_rdata_valid :  STD_LOGIC;
                signal last_wdata_req :  STD_LOGIC;
                signal max_col_value :  STD_LOGIC_VECTOR (9 DOWNTO 0);
                signal p_burst_begin :  STD_LOGIC;
                signal p_read_req :  STD_LOGIC;
                signal p_state_on :  STD_LOGIC;
                signal pause_be :  STD_LOGIC;
                signal pnf_persist1 :  STD_LOGIC;
                signal pnf_persist_compare :  STD_LOGIC;
                signal powerdn_on :  STD_LOGIC;
                signal rdata_valid_flag :  STD_LOGIC;
                signal rdata_valid_flag_reg :  STD_LOGIC;
                signal rdata_valid_flag_reg_2 :  STD_LOGIC;
                signal reached_max_address :  STD_LOGIC;
                signal read_req :  STD_LOGIC;
                signal reads_remaining :  STD_LOGIC_VECTOR (7 DOWNTO 0);
                signal reg_autopch :  STD_LOGIC;
                signal reset_address :  STD_LOGIC;
                signal reset_be :  STD_LOGIC;
                signal reset_data :  STD_LOGIC;
                signal restart_LFSR_n :  STD_LOGIC;
                signal row_addr :  STD_LOGIC_VECTOR (13 DOWNTO 0);
                signal selfrfsh_on :  STD_LOGIC;
                signal size :  STD_LOGIC_VECTOR (2 DOWNTO 0);
                signal state :  STD_LOGIC_VECTOR (4 DOWNTO 0);
                signal test_addr_pin :  STD_LOGIC;
                signal test_addr_pin_mode :  STD_LOGIC;
                signal test_addr_pin_on :  STD_LOGIC;
                signal test_dm_pin :  STD_LOGIC;
                signal test_dm_pin_mode :  STD_LOGIC;
                signal test_dm_pin_on :  STD_LOGIC;
                signal test_incomplete_writes :  STD_LOGIC;
                signal test_incomplete_writes_mode :  STD_LOGIC;
                signal test_incomplete_writes_on :  STD_LOGIC;
                signal test_seq_addr :  STD_LOGIC;
                signal test_seq_addr_mode :  STD_LOGIC;
                signal test_seq_addr_on :  STD_LOGIC;
                signal wait_first_write_data :  STD_LOGIC;
                signal wdata :  STD_LOGIC_VECTOR (15 DOWNTO 0);
                signal wdata_req :  STD_LOGIC;
                signal write_req :  STD_LOGIC;
                signal writes_remaining :  STD_LOGIC_VECTOR (7 DOWNTO 0);

begin

  --

  --Turn on this mode to test sequential address
  test_seq_addr_on <= std_logic'('1');
  --Turn on this mode to test all address pins by a One-hot pattern address generator
  test_addr_pin_on <= std_logic'('1');
  --Turn on this mode to make use of dm pins
  test_dm_pin_on <= std_logic'('1');
  --Turn on this mode to exercise write process in Full rate, size 1 or DDR3 Half rate, size 1
  test_incomplete_writes_on <= std_logic'('1');
  --restart_LFSR_n is an active low signal, set it to 1'b0 to restart LFSR data generator after a complete test
  restart_LFSR_n <= std_logic'('1');
  --Change COUNTER_VALUE to control the period of power down and self refresh mode
  COUNTER_VALUE <= std_logic_vector'("00000000000010010110");
  --Change MAX_ROW to test more or lesser row address in test_seq_addr_mode, maximum value is 2^(row bits) -1, while minimum value is 0
  MAX_ROW <= std_logic_vector'("00000000000011");
  --Change MAX_COL to test more or lesser column address in test_seq_addr_mode, maximum value is 2^(column bits) - (LOCAL_BURST_LEN_s * dwidth_ratio (aka half-rate (4) or full-rate (2))), while minimum value is 0 for Half rate and (LOCAL_BURST_LEN_s * dwidth_ratio) for Full rate
  MAX_COL <= std_logic_vector'("0000010000");
  --Decrease MAX_BANK to test lesser bank address, minimum value is 0
  MAX_BANK <= std_logic_vector'("11");
  --Decrease MAX_CHIPSEL to test lesser memory chip, minimum value is MIN_CHIPSEL
  MAX_CHIPSEL <= std_logic'('0');
  --

  MIN_CHIPSEL <= std_logic'('0');
  MAX_ROW_PIN <= A_REP(std_logic'('1'), 14);
  max_col_value <= A_EXT (A_WE_StdLogicVector((((std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(to_std_logic((((std_logic_vector'("000000000000000000000000000") & (addr_value)) = std_logic_vector'("00000000000000000000000000000010"))))))) = std_logic_vector'("00000000000000000000000000000000"))), (std_logic_vector'("00000000000000000000000") & (MAX_COL)), (((std_logic_vector'("00000000000000000000000") & (MAX_COL)) + std_logic_vector'("000000000000000000000000000000010")))), 10);
  local_autopch_req <= reg_autopch;
  autopch_req <= (test_addr_pin_mode OR test_dm_pin_mode) OR ((to_std_logic(((col_addr = max_col_value))) AND ((test_seq_addr_mode OR test_incomplete_writes_mode))));
  powerdn_on <= std_logic'('0');
  selfrfsh_on <= std_logic'('0');
  local_burstbegin <= burst_begin OR p_burst_begin;
  avalon_burst_mode <= std_logic'('1');
  --
  --One hot decoder for test_status signal
  test_status(0) <= test_seq_addr_mode;
  test_status(1) <= test_incomplete_writes_mode;
  test_status(2) <= test_dm_pin_mode;
  test_status(3) <= test_addr_pin_mode;
  test_status(4) <= std_logic'('0');
  test_status(5) <= std_logic'('0');
  test_status(6) <= autopch_req;
  test_status(7) <= internal_test_complete;
  p_read_req <= std_logic'('0');
  p_burst_begin <= std_logic'('0');
  local_cs_addr <= cs_addr;
  local_row_addr <= row_addr;
  local_bank_addr <= bank_addr;
  local_col_addr <= col_addr;
  local_write_req <= write_req;
  local_wdata <= wdata;
  local_read_req <= read_req OR p_read_req;
  wdata <= A_WE_StdLogicVector((((std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(reset_data))) = std_logic_vector'("00000000000000000000000000000000"))), dgen_data, std_logic_vector'("0000000000000000"));
  --The LOCAL_BURST_LEN_s is a signal used insted of the parameter LOCAL_BURST_LEN
  LOCAL_BURST_LEN_s <= std_logic_vector'("010");
  --LOCAL INTERFACE (AVALON)
  wdata_req <= write_req AND local_ready;
  -- Generate new data (enable lfsr) when writing or reading valid data
  dgen_pause <= NOT ((((wdata_req AND NOT reset_data)) OR (local_rdata_valid)));
  enable_be <= (((wdata_req AND test_dm_pin_mode) AND NOT reset_data)) OR ((test_dm_pin_mode AND local_rdata_valid));
  pnf_per_byte <= compare_valid_reg;
  pause_be <= ((reset_data AND test_dm_pin_mode)) OR NOT test_dm_pin_mode;
  local_be <= be;
  local_size <= size;
  size <= A_WE_StdLogicVector((((std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(full_burst_on))) = std_logic_vector'("00000000000000000000000000000000"))), (std_logic_vector'("00") & (A_TOSTDLOGICVECTOR(std_logic'('1')))), LOCAL_BURST_LEN_s(2 DOWNTO 0));
  reached_max_address <= (((((test_dm_pin_mode OR test_addr_pin_mode) OR to_std_logic((state = std_logic_vector'("01001"))))) AND to_std_logic(((row_addr = MAX_ROW_PIN))))) OR (((((((test_seq_addr_mode OR test_incomplete_writes_mode)) AND to_std_logic(((col_addr = (max_col_value))))) AND to_std_logic(((row_addr = MAX_ROW)))) AND to_std_logic(((bank_addr = MAX_BANK)))) AND to_std_logic(((std_logic'(cs_addr) = std_logic'(MAX_CHIPSEL))))));
  addr_value <= A_EXT (A_WE_StdLogicVector((((std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR((((test_incomplete_writes_mode AND write_req) AND NOT full_burst_on))))) = std_logic_vector'("00000000000000000000000000000000"))), std_logic_vector'("00000000000000000000000000000100"), std_logic_vector'("00000000000000000000000000000010")), 5);
  pnf_persist_compare <= A_WE_StdLogic((((std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(rdata_valid_flag_reg_2))) = std_logic_vector'("00000000000000000000000000000000"))), std_logic'('1'), pnf_persist1);
  LFSRGEN_0_lfsr_inst : ddr2_ex_lfsr8
    generic map(
      seed => 1
    )
    port map(
            clk => clk,
            data => dgen_data(7 DOWNTO 0),
            enable => dgen_enable,
            ldata => dgen_ldata(7 DOWNTO 0),
            load => dgen_load,
            pause => dgen_pause,
            reset_n => reset_n
    );

  -- 8 bit comparator per local byte lane
  compare(0) <= to_std_logic((((dgen_data(7 DOWNTO 0) AND A_REP(be(0) , 8))) = local_rdata(7 DOWNTO 0)));
  LFSRGEN_1_lfsr_inst : ddr2_ex_lfsr8
    generic map(
      seed => 11
    )
    port map(
            clk => clk,
            data => dgen_data(15 DOWNTO 8),
            enable => dgen_enable,
            ldata => dgen_ldata(15 DOWNTO 8),
            load => dgen_load,
            pause => dgen_pause,
            reset_n => reset_n
    );

  -- 8 bit comparator per local byte lane
  compare(1) <= to_std_logic((((dgen_data(15 DOWNTO 8) AND A_REP(be(1) , 8))) = local_rdata(15 DOWNTO 8)));
  --
  -------------------------------------------------------------------
  --Main clocked process
  -------------------------------------------------------------------
  --Read / Write control state machine & address counter
  -------------------------------------------------------------------
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      --Reset - asynchronously force all register outputs LOW
      state <= std_logic_vector'("00000");
      write_req <= std_logic'('0');
      read_req <= std_logic'('0');
      burst_begin <= std_logic'('0');
      burst_beat_count <= std_logic_vector'("000");
      dgen_load <= std_logic'('0');
      wait_first_write_data <= std_logic'('0');
      internal_test_complete <= std_logic'('0');
      reset_data <= std_logic'('0');
      reset_be <= std_logic'('0');
      writes_remaining <= std_logic_vector'("00000000");
      reads_remaining <= std_logic_vector'("00000000");
      test_addr_pin <= std_logic'('0');
      test_dm_pin <= std_logic'('0');
      test_seq_addr <= std_logic'('0');
      test_incomplete_writes <= std_logic'('0');
      test_addr_pin_mode <= std_logic'('0');
      test_dm_pin_mode <= std_logic'('0');
      test_seq_addr_mode <= std_logic'('0');
      test_incomplete_writes_mode <= std_logic'('0');
      full_burst_on <= std_logic'('1');
      p_state_on <= std_logic'('0');
      dgen_enable <= std_logic'('1');
    elsif clk'event and clk = '1' then
      if std_logic'((write_req AND local_ready)) = '1' then 
        if std_logic'(wdata_req) = '1' then 
          writes_remaining <= A_EXT (((std_logic_vector'("00000000000000000000000000") & (writes_remaining)) + (std_logic_vector'("0") & ((((std_logic_vector'("000000000000000000000000000000") & (size)) - std_logic_vector'("000000000000000000000000000000001")))))), 8);
        else
          writes_remaining <= A_EXT (((std_logic_vector'("0") & (writes_remaining)) + (std_logic_vector'("000000") & (size))), 8);
        end if;
      elsif std_logic'(((wdata_req) AND to_std_logic((((std_logic_vector'("000000000000000000000000") & (writes_remaining))>std_logic_vector'("00000000000000000000000000000000")))))) = '1' then 
        --size
        writes_remaining <= A_EXT (((std_logic_vector'("0") & (writes_remaining)) - (std_logic_vector'("00000000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 8);
      else
        writes_remaining <= writes_remaining;
      end if;
      if std_logic'((((read_req OR p_read_req)) AND local_ready)) = '1' then 
        if std_logic'(local_rdata_valid) = '1' then 
          reads_remaining <= A_EXT (((std_logic_vector'("00000000000000000000000000") & (reads_remaining)) + (std_logic_vector'("0") & ((((std_logic_vector'("000000000000000000000000000000") & (size)) - std_logic_vector'("000000000000000000000000000000001")))))), 8);
        else
          reads_remaining <= A_EXT (((std_logic_vector'("0") & (reads_remaining)) + (std_logic_vector'("000000") & (size))), 8);
        end if;
      elsif std_logic'(((local_rdata_valid) AND to_std_logic((((std_logic_vector'("000000000000000000000000") & (reads_remaining))>std_logic_vector'("00000000000000000000000000000000")))))) = '1' then 
        reads_remaining <= A_EXT (((std_logic_vector'("0") & (reads_remaining)) - (std_logic_vector'("00000000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 8);
      else
        reads_remaining <= reads_remaining;
      end if;
      case state is
          when std_logic_vector'("00000") => 
              test_addr_pin <= test_addr_pin_on;
              test_dm_pin <= test_dm_pin_on;
              test_seq_addr <= test_seq_addr_on;
              test_incomplete_writes <= test_incomplete_writes_on;
              internal_test_complete <= std_logic'('0');
              state <= std_logic_vector'("00001");
          -- when std_logic_vector'("00000") 
      
          when std_logic_vector'("00001") => 
              --Reset just in case!
              reset_address <= std_logic'('0');
              reset_be <= std_logic'('0');
              write_req <= std_logic'('1');
              writes_remaining <= std_logic_vector'("0000000") & (A_TOSTDLOGICVECTOR(std_logic'('0')));
              reads_remaining <= std_logic_vector'("0000000") & (A_TOSTDLOGICVECTOR(std_logic'('0')));
              wait_first_write_data <= std_logic'('1');
              dgen_enable <= std_logic'('1');
              if std_logic'(test_seq_addr) = std_logic'(std_logic'('1')) then 
                test_seq_addr_mode <= std_logic'('1');
                if (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000000") then 
                  state <= std_logic_vector'("00101");
                  burst_begin <= std_logic'('1');
                elsif (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000001") then 
                  state <= std_logic_vector'("01101");
                  burst_begin <= std_logic'('1');
                end if;
              elsif std_logic'(test_incomplete_writes) = std_logic'(std_logic'('1')) then 
                full_burst_on <= std_logic'('0');
                test_incomplete_writes_mode <= std_logic'('1');
                state <= std_logic_vector'("00101");
                if (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000001") then 
                  burst_begin <= std_logic'('1');
                end if;
              elsif std_logic'(test_dm_pin) = std_logic'(std_logic'('1')) then 
                reset_data <= std_logic'('1');
                test_dm_pin_mode <= std_logic'('1');
                if (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000000") then 
                  burst_begin <= std_logic'('1');
                  state <= std_logic_vector'("00010");
                else
                  burst_begin <= std_logic'('1');
                  state <= std_logic_vector'("01010");
                end if;
              elsif std_logic'(test_addr_pin) = std_logic'(std_logic'('1')) then 
                test_addr_pin_mode <= std_logic'('1');
                if (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000000") then 
                  burst_begin <= std_logic'('1');
                  state <= std_logic_vector'("00101");
                elsif (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000001") then 
                  state <= std_logic_vector'("01101");
                  burst_begin <= std_logic'('1');
                end if;
              else
                write_req <= std_logic'('0');
                wait_first_write_data <= std_logic'('0');
                state <= std_logic_vector'("01001");
              end if;
          -- when std_logic_vector'("00001") 
      
          when std_logic_vector'("01010") => 
              wait_first_write_data <= std_logic'('0');
              burst_begin <= std_logic'('0');
              if std_logic'((write_req AND local_ready)) = '1' then 
                burst_beat_count <= A_EXT (((std_logic_vector'("0") & (burst_beat_count)) + (std_logic_vector'("000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 3);
                state <= std_logic_vector'("01011");
              end if;
          -- when std_logic_vector'("01010") 
      
          when std_logic_vector'("01011") => 
              if std_logic'((write_req AND local_ready)) = '1' then 
                if (std_logic_vector'("0") & (burst_beat_count)) = ((std_logic_vector'("0") & (size)) - (std_logic_vector'("000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))) then 
                  burst_beat_count <= std_logic_vector'("000");
                  burst_begin <= std_logic'('1');
                  if std_logic'(reached_max_address) = '1' then 
                    state <= std_logic_vector'("01100");
                  else
                    state <= std_logic_vector'("01010");
                  end if;
                else
                  burst_beat_count <= A_EXT (((std_logic_vector'("0") & (burst_beat_count)) + (std_logic_vector'("000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 3);
                end if;
              end if;
          -- when std_logic_vector'("01011") 
      
          when std_logic_vector'("01100") => 
              burst_begin <= std_logic'('0');
              if std_logic'((write_req AND local_ready)) = '1' then 
                state <= std_logic_vector'("00011");
              end if;
          -- when std_logic_vector'("01100") 
      
          when std_logic_vector'("01101") => 
              wait_first_write_data <= std_logic'('0');
              burst_begin <= std_logic'('0');
              reset_be <= std_logic'('0');
              if std_logic'((write_req AND local_ready)) = '1' then 
                burst_beat_count <= A_EXT (((std_logic_vector'("0") & (burst_beat_count)) + (std_logic_vector'("000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 3);
                state <= std_logic_vector'("01110");
              end if;
          -- when std_logic_vector'("01101") 
      
          when std_logic_vector'("01110") => 
              if std_logic'((write_req AND local_ready)) = '1' then 
                if (std_logic_vector'("0") & (burst_beat_count)) = ((std_logic_vector'("0") & (size)) - (std_logic_vector'("000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))) then 
                  burst_beat_count <= std_logic_vector'("000");
                  burst_begin <= std_logic'('1');
                  if std_logic'(reached_max_address) = '1' then 
                    state <= std_logic_vector'("01111");
                  else
                    state <= std_logic_vector'("01101");
                  end if;
                else
                  burst_beat_count <= A_EXT (((std_logic_vector'("0") & (burst_beat_count)) + (std_logic_vector'("000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 3);
                end if;
              end if;
          -- when std_logic_vector'("01110") 
      
          when std_logic_vector'("01111") => 
              if std_logic'((write_req AND local_ready)) = '1' then 
                reset_address <= std_logic'('1');
                burst_begin <= std_logic'('0');
                state <= std_logic_vector'("00110");
              end if;
          -- when std_logic_vector'("01111") 
      
          when std_logic_vector'("10000") => 
              dgen_load <= std_logic'('0');
              reset_be <= std_logic'('0');
              if std_logic'(local_ready) = std_logic'(std_logic'('0')) then 
                read_req <= std_logic'('1');
                burst_begin <= std_logic'('0');
              elsif std_logic'((local_ready AND read_req)) = '1' then 
                if std_logic'(reached_max_address) = '1' then 
                  read_req <= std_logic'('0');
                  burst_begin <= std_logic'('0');
                  state <= std_logic_vector'("01000");
                else
                  read_req <= std_logic'('1');
                  burst_begin <= std_logic'('1');
                end if;
              end if;
          -- when std_logic_vector'("10000") 
      
          when std_logic_vector'("00010") => 
              wait_first_write_data <= std_logic'('0');
              if std_logic'((write_req AND local_ready)) = '1' then 
                if std_logic'(reached_max_address) = '1' then 
                  write_req <= std_logic'('0');
                  burst_begin <= std_logic'('0');
                  state <= std_logic_vector'("00011");
                end if;
              end if;
          -- when std_logic_vector'("00010") 
      
          when std_logic_vector'("00011") => 
              if (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000000") then 
                if std_logic'(NOT(wdata_req)) = '1' then 
                  if (std_logic_vector'("000000000000000000000000") & (writes_remaining)) = std_logic_vector'("00000000000000000000000000000000") then 
                    reset_be <= std_logic'('1');
                    reset_address <= std_logic'('1');
                    dgen_load <= std_logic'('1');
                    state <= std_logic_vector'("00100");
                  end if;
                end if;
              elsif std_logic'((write_req AND local_ready)) = '1' then 
                reset_be <= std_logic'('1');
                write_req <= std_logic'('0');
                reset_address <= std_logic'('1');
                dgen_load <= std_logic'('1');
                state <= std_logic_vector'("00100");
              end if;
          -- when std_logic_vector'("00011") 
      
          when std_logic_vector'("00100") => 
              reset_address <= std_logic'('0');
              dgen_load <= std_logic'('0');
              reset_be <= std_logic'('0');
              reset_data <= std_logic'('0');
              write_req <= std_logic'('1');
              if (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000000") then 
                burst_begin <= std_logic'('1');
                state <= std_logic_vector'("00101");
              else
                burst_begin <= std_logic'('1');
                state <= std_logic_vector'("01101");
              end if;
          -- when std_logic_vector'("00100") 
      
          when std_logic_vector'("00101") => 
              wait_first_write_data <= std_logic'('0');
              if std_logic'(local_ready) = std_logic'(std_logic'('0')) then 
                write_req <= std_logic'('1');
                burst_begin <= std_logic'('0');
              elsif std_logic'((write_req AND local_ready)) = '1' then 
                if std_logic'(reached_max_address) = '1' then 
                  reset_address <= std_logic'('1');
                  write_req <= std_logic'('0');
                  burst_begin <= std_logic'('0');
                  state <= std_logic_vector'("00110");
                  if std_logic'(test_incomplete_writes_mode) = '1' then 
                    full_burst_on <= std_logic'('1');
                  end if;
                else
                  write_req <= std_logic'('1');
                  burst_begin <= std_logic'('1');
                end if;
              end if;
          -- when std_logic_vector'("00101") 
      
          when std_logic_vector'("00110") => 
              reset_address <= std_logic'('0');
              if (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(avalon_burst_mode))) = std_logic_vector'("00000000000000000000000000000000") then 
                if (std_logic_vector'("000000000000000000000000") & (writes_remaining)) = std_logic_vector'("00000000000000000000000000000000") then 
                  dgen_load <= std_logic'('1');
                  reset_be <= std_logic'('1');
                  read_req <= std_logic'('1');
                  burst_begin <= std_logic'('1');
                  state <= std_logic_vector'("00111");
                end if;
              elsif std_logic'(test_incomplete_writes_mode) = '1' then 
                dgen_load <= std_logic'('1');
                read_req <= std_logic'('1');
                burst_begin <= std_logic'('1');
                state <= std_logic_vector'("10000");
              elsif std_logic'((write_req AND local_ready)) = '1' then 
                write_req <= std_logic'('0');
                dgen_load <= std_logic'('1');
                reset_be <= std_logic'('1');
                read_req <= std_logic'('1');
                burst_begin <= std_logic'('1');
                state <= std_logic_vector'("10000");
              end if;
          -- when std_logic_vector'("00110") 
      
          when std_logic_vector'("00111") => 
              dgen_load <= std_logic'('0');
              reset_be <= std_logic'('0');
              if std_logic'((local_ready AND read_req)) = '1' then 
                if std_logic'(reached_max_address) = '1' then 
                  read_req <= std_logic'('0');
                  burst_begin <= std_logic'('0');
                  state <= std_logic_vector'("01000");
                end if;
              end if;
          -- when std_logic_vector'("00111") 
      
          when std_logic_vector'("01000") => 
              if reads_remaining = (std_logic_vector'("0000000") & (A_TOSTDLOGICVECTOR(std_logic'('0')))) then 
                reset_address <= std_logic'('1');
                if std_logic'(test_seq_addr) = '1' then 
                  test_seq_addr <= std_logic'('0');
                  test_seq_addr_mode <= std_logic'('0');
                  state <= std_logic_vector'("00001");
                elsif std_logic'(test_incomplete_writes) = '1' then 
                  test_incomplete_writes <= std_logic'('0');
                  test_incomplete_writes_mode <= std_logic'('0');
                  state <= std_logic_vector'("00001");
                elsif std_logic'(test_dm_pin) = '1' then 
                  test_dm_pin <= std_logic'('0');
                  test_dm_pin_mode <= std_logic'('0');
                  state <= std_logic_vector'("00001");
                elsif std_logic'(test_addr_pin) = '1' then 
                  test_addr_pin_mode <= std_logic'('0');
                  dgen_load <= std_logic'('1');
                  state <= std_logic_vector'("01001");
                else
                  state <= std_logic_vector'("01001");
                end if;
              end if;
          -- when std_logic_vector'("01000") 
      
          when std_logic_vector'("01001") => 
              reset_address <= std_logic'('0');
              reset_be <= std_logic'('0');
              dgen_load <= std_logic'('0');
              if (std_logic'(powerdn_on) = std_logic'(std_logic'('0'))) AND (std_logic'(selfrfsh_on) = std_logic'(std_logic'('0'))) then 
                internal_test_complete <= std_logic'('1');
                p_state_on <= std_logic'('0');
                dgen_enable <= restart_LFSR_n;
                state <= std_logic_vector'("00000");
              elsif std_logic'((reached_max_address AND to_std_logic(((std_logic_vector'("000000000000000000000000") & (reads_remaining)) = std_logic_vector'("00000000000000000000000000000000"))))) = '1' then 
                p_state_on <= std_logic'('1');
                reset_address <= std_logic'('1');
                reset_be <= std_logic'('1');
                dgen_load <= std_logic'('1');
              end if;
          -- when std_logic_vector'("01001") 
      
          when others => 
          -- when others 
      
      end case; -- state
    end if;

  end process;

  --
  -------------------------------------------------------------------
  --Logics that detect the first read data
  -------------------------------------------------------------------
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      rdata_valid_flag <= std_logic'('0');
    elsif clk'event and clk = '1' then
      if std_logic'(local_rdata_valid) = '1' then 
        rdata_valid_flag <= std_logic'('1');
      end if;
    end if;

  end process;

  --
  -------------------------------------------------------------------
  --Address Generator Process
  -------------------------------------------------------------------
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      cs_addr <= std_logic'('0');
      bank_addr <= std_logic_vector'("00");
      row_addr <= std_logic_vector'("00000000000000");
      col_addr <= std_logic_vector'("0000000000");
    elsif clk'event and clk = '1' then
      if std_logic'(reset_address) = '1' then 
        cs_addr <= MIN_CHIPSEL;
        row_addr <= std_logic_vector'("00000000000000");
        bank_addr <= std_logic_vector'("00");
        col_addr <= std_logic_vector'("0000000000");
      elsif std_logic'((((((((local_ready AND write_req) AND ((test_dm_pin_mode OR test_addr_pin_mode)))) AND to_std_logic((((((state = std_logic_vector'("00010")) OR (state = std_logic_vector'("00101"))) OR (state = std_logic_vector'("01010"))) OR (state = std_logic_vector'("01101"))))))) OR (((((local_ready AND read_req) AND ((test_dm_pin_mode OR test_addr_pin_mode)))) AND to_std_logic((((state = std_logic_vector'("00111")) OR (state = std_logic_vector'("10000")))))))) OR ((((local_ready AND p_read_req)) AND to_std_logic(((state = std_logic_vector'("01001")))))))) = '1' then 
        col_addr(9 DOWNTO 2) <= Std_Logic_Vector'(col_addr(8 DOWNTO 2) & A_ToStdLogicVector(col_addr(9)));
        row_addr(13 DOWNTO 0) <= Std_Logic_Vector'(row_addr(12 DOWNTO 0) & A_ToStdLogicVector(row_addr(13)));
        if row_addr = std_logic_vector'("00000000000000") then 
          col_addr <= std_logic_vector'("0000000100");
          row_addr <= std_logic_vector'("00000000000001");
        elsif row_addr = Std_Logic_Vector'(A_ToStdLogicVector(std_logic'('1')) & A_REP(std_logic'('0'), 13)) then 
          col_addr <= A_REP(std_logic'('1'), 7) & A_REP(std_logic'('0'), 3);
          row_addr <= A_REP(std_logic'('1'), 13) & A_ToStdLogicVector(std_logic'('0'));
        elsif row_addr = Std_Logic_Vector'(A_ToStdLogicVector(std_logic'('0')) & A_REP(std_logic'('1'), 13)) then 
          col_addr <= A_REP(std_logic'('1'), 8) & A_REP(std_logic'('0'), 2);
          row_addr <= A_REP(std_logic'('1'), 14);
        end if;
        if bank_addr = MAX_BANK then 
          bank_addr <= std_logic_vector'("00");
        else
          bank_addr <= A_EXT (((std_logic_vector'("0") & (bank_addr)) + (std_logic_vector'("00") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 2);
        end if;
        if std_logic'(cs_addr) = std_logic'(MAX_CHIPSEL) then 
          cs_addr <= MIN_CHIPSEL;
        else
          cs_addr <= Vector_To_Std_Logic(((std_logic_vector'("0") & (A_TOSTDLOGICVECTOR(cs_addr))) + (std_logic_vector'("0") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))));
        end if;
      elsif std_logic'((((((local_ready AND write_req) AND ((test_seq_addr_mode OR test_incomplete_writes_mode))) AND to_std_logic((((((state = std_logic_vector'("00010")) OR (state = std_logic_vector'("00101"))) OR (state = std_logic_vector'("01010"))) OR (state = std_logic_vector'("01101"))))))) OR (((((local_ready AND read_req) AND ((test_seq_addr_mode OR test_incomplete_writes_mode)))) AND to_std_logic((((state = std_logic_vector'("00111")) OR (state = std_logic_vector'("10000"))))))))) = '1' then 
        if col_addr>=max_col_value then 
          col_addr <= std_logic_vector'("0000000000");
          if row_addr = MAX_ROW then 
            row_addr <= std_logic_vector'("00000000000000");
            if bank_addr = MAX_BANK then 
              bank_addr <= std_logic_vector'("00");
              if std_logic'(cs_addr) = std_logic'(MAX_CHIPSEL) then 
                --reached_max_count <= TRUE
                --(others => '0')
                cs_addr <= MIN_CHIPSEL;
              else
                cs_addr <= Vector_To_Std_Logic(((std_logic_vector'("0") & (A_TOSTDLOGICVECTOR(cs_addr))) + (std_logic_vector'("0") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))));
              end if;
            else
              bank_addr <= A_EXT (((std_logic_vector'("0") & (bank_addr)) + (std_logic_vector'("00") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 2);
            end if;
          else
            row_addr <= A_EXT (((std_logic_vector'("0") & (row_addr)) + (std_logic_vector'("00000000000000") & (A_TOSTDLOGICVECTOR(std_logic'('1'))))), 14);
          end if;
        else
          col_addr <= A_EXT (((std_logic_vector'("0") & (col_addr)) + (std_logic_vector'("000000") & (addr_value))), 10);
        end if;
      end if;
    end if;

  end process;

  --
  -------------------------------------------------------------------
  --Byte Enable Generator Process
  -------------------------------------------------------------------
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      be <= A_REP(std_logic'('1'), 2);
    elsif clk'event and clk = '1' then
      if std_logic'(reset_be) = '1' then 
        be <= std_logic_vector'("01");
      elsif std_logic'(enable_be) = '1' then 
        be(1 DOWNTO 0) <= Std_Logic_Vector'(A_ToStdLogicVector(be(0)) & A_ToStdLogicVector(be(1)));
      elsif std_logic'(pause_be) = '1' then 
        be <= A_REP(std_logic'('1'), 2);
      else
        be <= be;
      end if;
    end if;

  end process;

  --------------------------------------------------------------
  --Put autopch_req high at the last column
  --Making use of autoprecharge feature
  --------------------------------------------------------------
  process (autopch_req)
  begin
      if (std_logic_vector'("0000000000000000000000000000000") & (A_TOSTDLOGICVECTOR(autopch_req))) = std_logic_vector'("00000000000000000000000000000001") then 
        reg_autopch <= std_logic'('1');
      else
        reg_autopch <= std_logic'('0');
      end if;

  end process;

  --------------------------------------------------------------
  --LFSR re-load data storage
  --Comparator masking and test pass signal generation
  --------------------------------------------------------------
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      dgen_ldata <= std_logic_vector'("0000000000000000");
      last_wdata_req <= std_logic'('0');
      --all ones
      compare_valid <= A_REP(std_logic'('1'), 2);
      --all ones
      compare_valid_reg <= A_REP(std_logic'('1'), 2);
      pnf_persist <= std_logic'('0');
      pnf_persist1 <= std_logic'('0');
      --all ones
      compare_reg <= A_REP(std_logic'('1'), 2);
      last_rdata_valid <= std_logic'('0');
      rdata_valid_flag_reg <= std_logic'('0');
      rdata_valid_flag_reg_2 <= std_logic'('0');
    elsif clk'event and clk = '1' then
      last_wdata_req <= wdata_req;
      last_rdata_valid <= local_rdata_valid;
      rdata_valid_flag_reg <= rdata_valid_flag;
      rdata_valid_flag_reg_2 <= rdata_valid_flag_reg;
      compare_reg <= compare;
      if std_logic'(wait_first_write_data) = '1' then 
        dgen_ldata <= dgen_data;
      end if;
      --Enable the comparator result when read data is valid
      if std_logic'(last_rdata_valid) = '1' then 
        compare_valid <= compare_reg;
      end if;
      --Create the overall persistent passnotfail output
      if std_logic'(((and_reduce(compare_valid) AND rdata_valid_flag_reg) AND pnf_persist_compare)) = '1' then 
        pnf_persist1 <= std_logic'('1');
      else
        pnf_persist1 <= std_logic'('0');
      end if;
      --Extra register stage to help Tco / Fmax on comparator output pins
      compare_valid_reg <= compare_valid;
      pnf_persist <= pnf_persist1;
    end if;

  end process;

  --vhdl renameroo for output signals
  test_complete <= internal_test_complete;

end europa;

