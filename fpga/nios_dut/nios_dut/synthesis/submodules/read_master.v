// (C) 2001-2015 Altera Corporation. All rights reserved.
// Your use of Altera Corporation's design tools, logic functions and other 
// software and tools, and its AMPP partner logic functions, and any output 
// files any of the foregoing (including device programming or simulation 
// files), and any associated documentation or information are expressly subject 
// to the terms and conditions of the Altera Program License Subscription 
// Agreement, Altera MegaCore Function License Agreement, or other applicable 
// license agreement, including, without limitation, that your use is for the 
// sole purpose of programming logic devices manufactured by Altera and sold by 
// Altera or its authorized distributors.  Please refer to the applicable 
// agreement for further details.


/*
  This read master module is responsible for reading data from memory and writing
  the contents out to a streaming source port.  It is controlled by a streaming
  sink port called the 'command port'.  Any information that must be communicated
  back to a host such as the state of the master (reset/stop) is made available by the
  streaming source port called the 'response port'.

  There are various parameters to control the synthesis of this hardware
  either for functionality changes or speed/resource optimizations.  Some
  of the parameters will be hidden in the component GUI since they are derived
  from some other parameters.  When this master module is used in a MM to MM
  transfer disable the packet support since the packet hardware is not needed.

  In order to increase the Fmax you should enable only full accesses so that
  the unaligned access and byte enable blocks can be reduced to wires.  Also
  only configure the length width to be as wide as you need as it will typically
  be the critical path of this module.  


  Revision History:

  1.0 Initial version which used a simple exported hand shake control scheme.

  2.0 Added support for unaligned accesses, stride, and streaming

  2.1 Fixed bugs in the control logic which was causing too many reads to be posted

  2.2 Added burst support and renamed the top level module to read master

  2.3 Added additional conditional code for 8-bit case to avoid synthesis issues.
  
  2.4 Corrected burst bug that prevented full bursts from being presented to the
      fabric.  Corrected the stop/reset logic to ensure masters can be stopped
      or reset while idle.
  
  2.5 Added early done support for non unaligned or non packet based transfers
  
  2.6 Fixed a flow control issue in the pending reads counter and too many reads pending
      signal to avoid potential FIFO overflow issues.  The read master now requires the
      FIFO depth to be 4x the maximum burst count setting.
      
  2.7 Added 64-bit addressing.
  
  2.8 Fixed another flow control issue when unaligned accesses is enabled.  Unaligned
      accesses causes one more clock cycle of delay between the read data and the FIFO
      so one more burst of room in the FIFO needs to be pre-allocated (3x instead of 2x)

*/


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 


module read_master (
  clk,
  reset,

  // descriptor commands sink port
  snk_command_data,
  snk_command_valid,
  snk_command_ready,

  // response source port
  src_response_data,
  src_response_valid,
  src_response_ready,

  // data path sink port
  src_data,
  src_valid,
  src_ready,
  src_sop,
  src_eop,
  src_empty,
  src_error,
  src_channel,

  // data path master port
  master_address,
  master_read,
  master_byteenable,
  master_readdata,
  master_waitrequest,
  master_readdatavalid,
  master_burstcount
);

  parameter UNALIGNED_ACCESSES_ENABLE = 0;        // when enabled allows transfers to begin from off word boundaries
  parameter ONLY_FULL_ACCESS_ENABLE = 0;          // when enabled allows transfers to end with partial access, master achieve a much higher fmax when this is enabled
  parameter STRIDE_ENABLE = 0;                    // stride support can only be enabled when unaligned accesses is disabled
  parameter STRIDE_WIDTH = 1;                     // when stride support is enabled this value controls the rate in which the address increases (in words), the stride width + log2(byte enable width) + 1 cannot exceed address width               
  parameter PACKET_ENABLE = 0;
  parameter ERROR_ENABLE = 0;
  parameter ERROR_WIDTH = 8;                      // must be between 1-8, this will only be enabled in the GUI when error enable is turned on
  parameter CHANNEL_ENABLE = 0;
  parameter CHANNEL_WIDTH = 8;                    // must be between 1-8, this will only be enabled in the GUI when the channel enable is turned on
  parameter DATA_WIDTH = 32;
  parameter BYTE_ENABLE_WIDTH = 4;                // set by the .tcl file (hidden in GUI)
  parameter BYTE_ENABLE_WIDTH_LOG2 = 2;           // set by the .tcl file (hidden in GUI)
  parameter ADDRESS_WIDTH = 32;                   // set in the .tcl file (hidden in GUI) by the address span of the master
  parameter LENGTH_WIDTH = 32;                    // GUI setting with warning if ADDRESS_WIDTH < LENGTH_WIDTH (waste of logic for the length counter)
  parameter FIFO_DEPTH = 32;
  parameter FIFO_DEPTH_LOG2 = 5;                  // set by the .tcl file (hidden in GUI)
  parameter FIFO_SPEED_OPTIMIZATION = 1;          // set by the .tcl file (hidden in GUI)  The default will be on since it only impacts the latency of the entire transfer by 1 clock cycle and adds very little additional logic.
  parameter SYMBOL_WIDTH = 8;                     // set in the .tcl file (hidden in GUI)
  parameter NUMBER_OF_SYMBOLS = 4;                // set in the .tcl file (hidden in GUI)
  parameter NUMBER_OF_SYMBOLS_LOG2 = 2;           // set by the .tcl file (hidden in GUI)
  parameter BURST_ENABLE = 0;                     // when enabled stride must be disabled, 1 to enable, 0 to disable
  parameter MAX_BURST_COUNT = 2;                  // must be a power of 2, when BURST_ENABLE = 0 set maximum_burst_count to 1 (will be automatically done by .tcl file)
  parameter MAX_BURST_COUNT_WIDTH = 2;            // set by the .tcl file (hidden in GUI) = log2(maximum_burst_count) + 1
  parameter PROGRAMMABLE_BURST_ENABLE = 0;        // when enabled the user must set the burst count, if 0 is set then the value "maximum_burst_count" will be used instead
  parameter BURST_WRAPPING_SUPPORT = 1;           // will only be used when bursting is enabled.  This cannot be enabled with programmable burst capabilities.  Enabling it will make sure the master gets back into burst alignment (data width in bytes * maximum burst count alignment)

  localparam FIFO_USE_MEMORY = 1;                 // set to 0 to use LEs instead, not exposed since FPGAs have a lot of memory these days
  localparam BIG_ENDIAN_ACCESS = 0;               // hiding this since it can blow your foot off if you are not careful.  It's big endian with respect to the write master width and not necessarily to the width of the data type used by a host CPU.
  // handy mask for seperating the word address from the byte address bits, so for 32 bit masters this mask is 0x3, for 64 bit masters it'll be 0x7
  localparam LSB_MASK = {BYTE_ENABLE_WIDTH_LOG2{1'b1}};
  // when packet data is supported then we need to buffer the empty, eop, sop, error, and channel bits
  localparam FIFO_WIDTH = DATA_WIDTH + NUMBER_OF_SYMBOLS_LOG2 + 2 + ERROR_WIDTH + CHANNEL_WIDTH;
  localparam ADDRESS_INCREMENT_WIDTH = (BYTE_ENABLE_WIDTH_LOG2 + MAX_BURST_COUNT_WIDTH + STRIDE_WIDTH);
  localparam FIXED_STRIDE = 1'b1;  // default stride distance used when stride is disabled.  1 means increment the address by a word (i.e. sequential transfer)


  input clk;
  input reset;

  // descriptor commands sink port
  input [255:0] snk_command_data;
  input snk_command_valid;
  output reg snk_command_ready;

  // response source port
  output wire [255:0] src_response_data;
  output reg src_response_valid;
  input src_response_ready;

  // data path source port
  output wire [DATA_WIDTH-1:0] src_data;
  output wire src_valid;
  input src_ready;
  output wire src_sop;
  output wire src_eop;
  output wire [NUMBER_OF_SYMBOLS_LOG2-1:0] src_empty;
  output wire [ERROR_WIDTH-1:0] src_error;
  output wire [CHANNEL_WIDTH-1:0] src_channel;

  // master inputs and outputs
  input master_waitrequest;
  output wire [ADDRESS_WIDTH-1:0] master_address;
  output wire master_read;
  output wire [BYTE_ENABLE_WIDTH-1:0] master_byteenable;
  input [DATA_WIDTH-1:0] master_readdata;
  input master_readdatavalid;
  output wire [MAX_BURST_COUNT_WIDTH-1:0] master_burstcount;

  // internal signals 
  wire [63:0] descriptor_address;
  wire [31:0] descriptor_length;
  wire [15:0] descriptor_stride;
  wire [7:0] descriptor_channel;
  wire descriptor_generate_sop;
  wire descriptor_generate_eop;
  wire [7:0] descriptor_error;
  wire [7:0] descriptor_programmable_burst_count;
  wire descriptor_early_done_enable;
  wire sw_stop_in;
  wire sw_reset_in;
  reg early_done_enable_d1;
  reg [ERROR_WIDTH-1:0] error_d1;
  reg [MAX_BURST_COUNT_WIDTH-1:0] programmable_burst_count_d1;
  wire [MAX_BURST_COUNT_WIDTH-1:0] maximum_burst_count;
  reg generate_sop_d1;
  reg generate_eop_d1;
  reg [ADDRESS_WIDTH-1:0] address_counter;
  reg [LENGTH_WIDTH-1:0] length_counter;
  reg [CHANNEL_WIDTH-1:0] channel_d1;
  reg [STRIDE_WIDTH-1:0] stride_d1;
  wire [STRIDE_WIDTH-1:0] stride_amount;  // either set to be stride_d1 or hardcoded to 1 depending on the parameterization
  reg [BYTE_ENABLE_WIDTH_LOG2-1:0] start_byte_address;  // used to determine how far out of alignment the master starts
  reg first_access;  // used to determine if the first read is occuring
  wire first_word_boundary_not_reached;  // set when the first access doesn't reach the next word boundary
  reg first_word_boundary_not_reached_d1;
  reg [FIFO_DEPTH_LOG2:0] pending_reads_counter;
  reg [FIFO_DEPTH_LOG2:0] pending_reads_mux;
  wire [FIFO_WIDTH-1:0] fifo_write_data;
  wire [FIFO_WIDTH-1:0] fifo_read_data;
  wire fifo_write;
  wire fifo_read;
  wire fifo_empty;
  wire fifo_full;
  wire [FIFO_DEPTH_LOG2-1:0] fifo_used;
  wire too_many_pending_reads;
  wire read_complete;  // handy signal for determining when a read has occured and completed
  wire address_increment_enable;
  wire [ADDRESS_INCREMENT_WIDTH-1:0] address_increment;  // amount of bytes to increment the address
  wire [ADDRESS_INCREMENT_WIDTH-1:0] bytes_to_transfer;
  wire short_first_access_enable;           // when starting unaligned and the amount of data to transfer reaches the next word boundary
  wire short_last_access_enable;            // when address is aligned (can be an unaligned buffer transfer) but the amount of data doesn't reach the next word boundary 
  wire short_first_and_last_access_enable;  // when starting unaligned and the amount of data to transfer doesn't reach the next word boundary
  wire [ADDRESS_INCREMENT_WIDTH-1:0] short_first_access_size;
  wire [ADDRESS_INCREMENT_WIDTH-1:0] short_last_access_size;
  wire [ADDRESS_INCREMENT_WIDTH-1:0] short_first_and_last_access_size;
  reg [ADDRESS_INCREMENT_WIDTH-1:0] bytes_to_transfer_mux;
  wire go;
  wire done;  // asserted when last read is issued
  reg done_d1;
  wire done_strobe;
  wire all_reads_returned;  // asserted when last read returns
  reg all_reads_returned_d1;
  wire all_reads_returned_strobe;
  reg all_reads_returned_strobe_d1;
  reg all_reads_returned_strobe_d2;  // used to trigger src_response_ready later than when the last read returns since the MM to ST has two pipeline stages
  wire [DATA_WIDTH-1:0] MM_to_ST_adapter_dataout;
  wire [DATA_WIDTH-1:0] MM_to_ST_adapter_dataout_rearranged;
  wire MM_to_ST_adapter_sop;
  wire MM_to_ST_adapter_eop;
  wire [NUMBER_OF_SYMBOLS_LOG2-1:0] MM_to_ST_adapter_empty;
  wire masked_sop;
  wire masked_eop;
  reg flush;
  reg stopped;
  wire length_sync_reset;
  wire set_src_response_valid;
  reg master_read_reg;


/********************************************* REGISTERS **************************************************/
  // registering descriptor information
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      error_d1 <= 0;
      generate_sop_d1 <= 0;
      generate_eop_d1 <= 0;
      channel_d1 <= 0;
      stride_d1 <= 0;
      programmable_burst_count_d1 <= 0;
      early_done_enable_d1 <= 0;
    end
    else if (go == 1)
    begin
      error_d1 <= descriptor_error[ERROR_WIDTH-1:0];
      generate_sop_d1 <= descriptor_generate_sop;
      generate_eop_d1 <= descriptor_generate_eop;
      channel_d1 <= descriptor_channel[CHANNEL_WIDTH-1:0];
      stride_d1 <= descriptor_stride[STRIDE_WIDTH-1:0];
      programmable_burst_count_d1 <= ((descriptor_programmable_burst_count == 0) | (descriptor_programmable_burst_count > 128)) ? MAX_BURST_COUNT : descriptor_programmable_burst_count;
      early_done_enable_d1 <= ((UNALIGNED_ACCESSES_ENABLE == 1) | (PACKET_ENABLE == 1))? 0 : descriptor_early_done_enable;  // early done cannot be used when unaligned data or packet support is enabled
    end
  end


  // master word increment counter
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      address_counter <= 0;
    end
    else
    begin
      if (go == 1)
      begin
        address_counter <= descriptor_address[ADDRESS_WIDTH-1:0];
      end
      else if (address_increment_enable == 1)
      begin
        address_counter <= address_counter + address_increment;
      end
    end
  end


  // master byte address, used to determine how far out of alignment the master began transfering data
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      start_byte_address <= 0;
    end
    else if (go == 1)
    begin
      start_byte_address <= descriptor_address[BYTE_ENABLE_WIDTH_LOG2-1:0];
    end
  end


  // first_access will be asserted only for the first read of a transaction, this will be used to determine what value will be used to increment the counters
  always @ (posedge clk or posedge reset)
  begin
    if (reset == 1)
    begin
      first_access <= 0;
    end
    else
    begin
      if (go == 1)
      begin
        first_access <= 1;
      end
      else if ((first_access == 1) & (address_increment_enable == 1))
      begin
        first_access <= 0;
      end
    end
  end


  // this register is used to determine if the first word boundary will be reached
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      first_word_boundary_not_reached_d1 <= 0;
    end
    else if (go == 1)
    begin
      first_word_boundary_not_reached_d1 <= first_word_boundary_not_reached;
    end
  end


  // master length logic, this will typically be the critical path followed by the FIFO
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      length_counter <= 0;
    end
    else
    begin
      if (length_sync_reset == 1)
      begin
        length_counter <= 0;
      end
      else if (go == 1)
      begin
        length_counter <= descriptor_length[LENGTH_WIDTH-1:0];
      end
      else if (address_increment_enable == 1)
      begin
        length_counter <= length_counter - bytes_to_transfer;  // not using address_increment because stride might be enabled
      end
    end
  end

	
  // the pending reads counter is used to determine how many outstanding reads are posted.  This will be used to determine
  // if more reads can be posted based on the number of unused words in the FIFO.
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      pending_reads_counter <= 0;
    end
    else
    begin
      pending_reads_counter <= pending_reads_mux;
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      done_d1 <= 1;    // master is done coming out of reset (need this to be set high so that done_strobe doesn't fire)
    end
    else
    begin
      done_d1 <= done;
    end
  end


  // this is the 'final done' condition, since reads are pipelined need to make sure they have all returned before the master is really done.
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      all_reads_returned_d1 <= 1;
    end
    else
    begin
      all_reads_returned_d1 <= all_reads_returned;
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset == 1)
    begin
      flush <= 0;
    end
    else
    begin
      if ((pending_reads_counter == 0) & (flush == 1))
      begin
        flush <= 0;
      end
      else if ((sw_reset_in == 1) & ((read_complete == 1) | (snk_command_ready == 1) | (master_read_reg == 0)))
      begin
        flush <= 1;  // will be used to reset the length counter to 0 and flush out pending reads (by letting them return without buffering them)
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      stopped <= 0;
    end
    else
    begin
      if ((sw_stop_in == 0) | (sw_reset_in == 1))
      begin
        stopped <= 0;
      end
      else if ((sw_stop_in == 1) & ((read_complete == 1) | (snk_command_ready == 1) | (master_read_reg == 0)))
      begin
        stopped <= 1;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      snk_command_ready <= 1;  // have to start ready to take commands
    end
    else
    begin
      if (go == 1)
      begin
        snk_command_ready <= 0;
      end
      else if ((src_response_ready == 1) & (src_response_valid == 1))  // need to make sure the response is popped before accepting more commands
      begin
        snk_command_ready <= 1;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      all_reads_returned_strobe_d1 <= 0;
      all_reads_returned_strobe_d2 <= 0;
    end
    else
    begin
      all_reads_returned_strobe_d1 <= all_reads_returned_strobe;
      all_reads_returned_strobe_d2 <= all_reads_returned_strobe_d1;
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      src_response_valid <= 0;
    end
    else
    begin
      if (flush == 1)
      begin
        src_response_valid <= 0;
      end
      else if (set_src_response_valid == 1)  // all the reads have returned with MM to ST adapter latency taken into consideration
      begin
        src_response_valid <= 1;  // will be set only once
      end
      else if ((src_response_valid == 1) & (src_response_ready == 1))
      begin
        src_response_valid <= 0;  // will be reset only once when the dispatcher captures the data
      end
    end
  end
  
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      master_read_reg <= 0;
    end
    else
    begin
      if ((done == 0) & (too_many_pending_reads == 0) & (sw_stop_in == 0) & (sw_reset_in == 0))
      begin
        master_read_reg <= 1;
      end
      else if ((done == 1) | ((read_complete == 1) & ((too_many_pending_reads == 1) | (sw_stop_in == 1))))
      begin
        master_read_reg <= 0;
      end 
    end
  end
/******************************************* END REGISTERS ************************************************/




/************************************** MODULE INSTANTIATIONS *********************************************/
  // This block is pipelined and can't throttle the reads
  MM_to_ST_Adapter the_MM_to_ST_adapter (
    .clk (clk),
    .reset (reset),
    .length (descriptor_length[LENGTH_WIDTH-1:0]),
    .length_counter (length_counter),
    .address (descriptor_address[ADDRESS_WIDTH-1:0]),
    .reads_pending (pending_reads_counter),
    .start (go),
    .readdata (master_readdata),
    .readdatavalid (master_readdatavalid),
    .fifo_data (MM_to_ST_adapter_dataout),
    .fifo_write (fifo_write),
    .fifo_empty (MM_to_ST_adapter_empty),
    .fifo_sop (MM_to_ST_adapter_sop),
    .fifo_eop (MM_to_ST_adapter_eop)
  );
  defparam the_MM_to_ST_adapter.DATA_WIDTH = DATA_WIDTH;
  defparam the_MM_to_ST_adapter.LENGTH_WIDTH = LENGTH_WIDTH;
  defparam the_MM_to_ST_adapter.ADDRESS_WIDTH = ADDRESS_WIDTH;
  defparam the_MM_to_ST_adapter.BYTE_ADDRESS_WIDTH = BYTE_ENABLE_WIDTH_LOG2;
  defparam the_MM_to_ST_adapter.READS_PENDING_WIDTH = FIFO_DEPTH_LOG2 + 1;
  defparam the_MM_to_ST_adapter.EMPTY_WIDTH = NUMBER_OF_SYMBOLS_LOG2;
  defparam the_MM_to_ST_adapter.PACKET_SUPPORT = PACKET_ENABLE;
  defparam the_MM_to_ST_adapter.UNALIGNED_ACCESS_ENABLE = UNALIGNED_ACCESSES_ENABLE;
  defparam the_MM_to_ST_adapter.FULL_WORD_ACCESS_ONLY = ONLY_FULL_ACCESS_ENABLE;


  // buffered sop, eop, empty, data (in that order).  sop, eop, and empty are only buffered when packet support is enabled
  scfifo the_master_to_st_fifo (
    .aclr (reset),
    .sclr (flush),
    .clock (clk),
    .data (fifo_write_data),
    .full (fifo_full),
    .empty (fifo_empty),
    .usedw (fifo_used),
    .q (fifo_read_data),
    .rdreq (fifo_read),
    .wrreq (fifo_write)
  );
  defparam the_master_to_st_fifo.lpm_width = FIFO_WIDTH;
  defparam the_master_to_st_fifo.lpm_numwords = FIFO_DEPTH;
  defparam the_master_to_st_fifo.lpm_widthu = FIFO_DEPTH_LOG2;
  defparam the_master_to_st_fifo.lpm_showahead = "ON";  // slower but doesn't require complex control logic to time with waitrequest
  defparam the_master_to_st_fifo.use_eab = (FIFO_USE_MEMORY == 1)? "ON" : "OFF";
  defparam the_master_to_st_fifo.add_ram_output_register = (FIFO_SPEED_OPTIMIZATION == 1)? "ON" : "OFF";
  defparam the_master_to_st_fifo.underflow_checking = "OFF";
  defparam the_master_to_st_fifo.overflow_checking = "OFF";


  // burst block that takes the length and short access enables and forms a burst count based on them.  If any of the short access bits are asserted the block will default to a burst count of 1
  read_burst_control the_read_burst_control (
    .address (master_address),
    .length (length_counter),
    .maximum_burst_count (maximum_burst_count),
    .short_first_access_enable (short_first_access_enable),
    .short_last_access_enable (short_last_access_enable),
    .short_first_and_last_access_enable (short_first_and_last_access_enable),
    .burst_count (master_burstcount)
  );
  defparam the_read_burst_control.BURST_ENABLE = BURST_ENABLE;
  defparam the_read_burst_control.BURST_COUNT_WIDTH = MAX_BURST_COUNT_WIDTH;
  defparam the_read_burst_control.WORD_SIZE_LOG2 = (DATA_WIDTH == 8)? 0 : BYTE_ENABLE_WIDTH_LOG2;  // need to make sure log2(word size) is 0 instead of 1 here when the data width is 8 bits
  defparam the_read_burst_control.ADDRESS_WIDTH = ADDRESS_WIDTH;
  defparam the_read_burst_control.LENGTH_WIDTH = LENGTH_WIDTH;
  defparam the_read_burst_control.BURST_WRAPPING_SUPPORT = BURST_WRAPPING_SUPPORT;



/************************************ END MODULE INSTANTIATIONS *******************************************/




/******************************** CONTROL AND COMBINATIONAL SIGNALS ***************************************/
  // breakout the descriptor information into more manageable names
  assign descriptor_address = {snk_command_data[140:109], snk_command_data[31:0]};  // 64-bit addressing support
  assign descriptor_length = snk_command_data[63:32];
  assign descriptor_channel = snk_command_data[71:64];
  assign descriptor_generate_sop = snk_command_data[72];
  assign descriptor_generate_eop = snk_command_data[73];
  assign descriptor_programmable_burst_count = snk_command_data[83:76];
  assign descriptor_stride = snk_command_data[99:84];
  assign descriptor_error = snk_command_data[107:100];
  assign descriptor_early_done_enable = snk_command_data[108];
  assign sw_stop_in = snk_command_data[74];
  assign sw_reset_in = snk_command_data[75];

  assign stride_amount = (STRIDE_ENABLE == 1)? stride_d1[STRIDE_WIDTH-1:0] : FIXED_STRIDE;  // hardcoding to FIXED_STRIDE when stride capabilities are disabled
  assign maximum_burst_count = (PROGRAMMABLE_BURST_ENABLE == 1)? programmable_burst_count_d1 : MAX_BURST_COUNT;

  // swap the bytes if big endian is enabled
  generate
    if (BIG_ENDIAN_ACCESS == 1)
    begin
      genvar j;
      for(j=0; j < DATA_WIDTH; j = j + 8)
      begin: byte_swap
        assign MM_to_ST_adapter_dataout_rearranged[j +8 -1: j] = MM_to_ST_adapter_dataout[DATA_WIDTH -j -1: DATA_WIDTH -j - 8];
      end
    end
    else
    begin
      assign MM_to_ST_adapter_dataout_rearranged = MM_to_ST_adapter_dataout;
    end
  endgenerate


  assign masked_sop = MM_to_ST_adapter_sop & generate_sop_d1;
  assign masked_eop = MM_to_ST_adapter_eop & generate_eop_d1;
  assign fifo_write_data = {error_d1, channel_d1, masked_sop, masked_eop, ((masked_eop == 1)? MM_to_ST_adapter_empty : {NUMBER_OF_SYMBOLS_LOG2{1'b0}} ), MM_to_ST_adapter_dataout_rearranged};


  // Avalon-ST is network order (a.k.a. big endian) so we need to reverse the symbols before sending them to the data stream
  generate
    genvar i;
    for(i = 0; i < DATA_WIDTH; i = i + SYMBOL_WIDTH)  // the data width is always a multiple of the symbol width
    begin: symbol_swap
      assign src_data[i +SYMBOL_WIDTH -1: i] = fifo_read_data[DATA_WIDTH -i -1: DATA_WIDTH -i - SYMBOL_WIDTH];
    end
  endgenerate

  // Format of internal FIFO data as following: {error, channel, sop, eop, empty, data}
  assign src_empty = (PACKET_ENABLE == 1)? fifo_read_data[(DATA_WIDTH + NUMBER_OF_SYMBOLS_LOG2 - 1) : DATA_WIDTH] : 0;
  assign src_eop = (PACKET_ENABLE == 1)? fifo_read_data[DATA_WIDTH + NUMBER_OF_SYMBOLS_LOG2] : 0;
  assign src_sop = (PACKET_ENABLE == 1)? fifo_read_data[DATA_WIDTH + NUMBER_OF_SYMBOLS_LOG2 + 1] : 0;
  assign src_channel = (CHANNEL_ENABLE == 1)? fifo_read_data[(DATA_WIDTH + NUMBER_OF_SYMBOLS_LOG2 + CHANNEL_WIDTH + 1): (DATA_WIDTH + NUMBER_OF_SYMBOLS_LOG2 + 2)] : 0;
  // case:220874 mSGDMA read master connects the wrong bits for the source error
  //assign src_error = (ERROR_ENABLE == 1)? fifo_read_data[(FIFO_WIDTH-1):(DATA_WIDTH + NUMBER_OF_SYMBOLS_LOG2 + ERROR_WIDTH + 2)] : 0;
  assign src_error = (ERROR_ENABLE == 1)? fifo_read_data[(FIFO_WIDTH-1):(DATA_WIDTH + NUMBER_OF_SYMBOLS_LOG2 + CHANNEL_WIDTH + 2)] : 0;
  
  assign short_first_access_size = BYTE_ENABLE_WIDTH - (address_counter & LSB_MASK);
  assign short_last_access_size = length_counter & LSB_MASK;
  assign short_first_and_last_access_size = length_counter & LSB_MASK;


  /* special case transfer enables and counter increment values (address and length counter)
    short_first_access_enable is for transfers that start unaligned but reach the next word boundary
    short_last_access_enable is for transfers that are not the first transfer but don't end on a word boundary
    short_first_and_last_access_enable is for transfers that start and end with a single transfer and don't end on a word boundary (aligned or unaligned)
  */
  generate
    if (UNALIGNED_ACCESSES_ENABLE == 1)
    begin
      assign short_first_access_enable = ((address_counter & LSB_MASK) != 0) & (first_access == 1) & (first_word_boundary_not_reached_d1 == 0);
      assign short_last_access_enable = (first_access == 0) & (length_counter < BYTE_ENABLE_WIDTH);
      assign short_first_and_last_access_enable = (first_access == 1) & (first_word_boundary_not_reached_d1 == 1);
      assign bytes_to_transfer = bytes_to_transfer_mux;
      assign address_increment = bytes_to_transfer_mux;  // can't use stride when unaligned accesses are enabled
    end
    else if (ONLY_FULL_ACCESS_ENABLE == 1)
    begin
      assign short_first_access_enable = 0;
      assign short_last_access_enable = 0;
      assign short_first_and_last_access_enable = 0;
      assign bytes_to_transfer = BYTE_ENABLE_WIDTH * master_burstcount;
      if (STRIDE_ENABLE == 1)
      begin
        assign address_increment = BYTE_ENABLE_WIDTH * stride_amount * master_burstcount;  // stride must be a static '1' when bursting is enabled
      end
      else
      begin
        assign address_increment = BYTE_ENABLE_WIDTH * master_burstcount;  // stride must be a static '1' when bursting is enabled
      end
    end
    else  // must be aligned but can end with any number of bytes
    begin
      assign short_first_access_enable = 0;
      assign short_last_access_enable = length_counter < BYTE_ENABLE_WIDTH;    // less than a word to transfer
      assign short_first_and_last_access_enable = 0;
      assign bytes_to_transfer = bytes_to_transfer_mux;
      if (STRIDE_ENABLE == 1)
      begin
        assign address_increment = BYTE_ENABLE_WIDTH * stride_amount * master_burstcount;  // stride must be a static '1' when bursting is enabled
      end
      else
      begin
        assign address_increment = BYTE_ENABLE_WIDTH * master_burstcount;  // stride must be a static '1' when bursting is enabled
      end      

    end
  endgenerate

  // the burst count will be 1 for all short accesses
  always @ (short_first_access_enable or short_last_access_enable or short_first_and_last_access_enable or short_first_access_size or short_last_access_size or short_first_and_last_access_size or master_burstcount)
  begin
    case ({short_first_and_last_access_enable, short_last_access_enable, short_first_access_enable})
      3'b001: bytes_to_transfer_mux = short_first_access_size;
      3'b010: bytes_to_transfer_mux = short_last_access_size;
      3'b100: bytes_to_transfer_mux = short_first_and_last_access_size;
      default: bytes_to_transfer_mux = BYTE_ENABLE_WIDTH * master_burstcount;  // this is the only time master_burstcount can be a value other than 1
    endcase
  end

  always @ (master_readdatavalid or read_complete or pending_reads_counter or master_burstcount)
  begin
    case ({master_readdatavalid, read_complete})
      2'b00: pending_reads_mux = pending_reads_counter;                               // no read posted and no read data returned
      2'b01: pending_reads_mux = (pending_reads_counter + master_burstcount);         // read posted and no read data returned
      2'b10: pending_reads_mux = (pending_reads_counter - 1'b1);                      // no read posted but read data returned
      2'b11: pending_reads_mux = (pending_reads_counter + master_burstcount - 1'b1);  // read posted and read data returned  
    endcase
  end

  assign src_valid = (fifo_empty == 0);
  assign first_word_boundary_not_reached = (descriptor_length < BYTE_ENABLE_WIDTH) & // length is less than the word size
                    (((descriptor_length & LSB_MASK) + (descriptor_address & LSB_MASK)) < BYTE_ENABLE_WIDTH);  // start address + length doesn't reach the next word boundary

  assign go = (snk_command_valid == 1) & (snk_command_ready == 1);  // go with be one cycle since done will be set to 0 on the next cycle (length will be non-zero)
  assign done = (length_counter == 0);  // all reads are posted but the master is not done since there could be reads pending
  assign done_strobe = (done == 1) & (done_d1 == 0);
  assign fifo_read = (src_valid == 1) & (src_ready == 1);
  assign length_sync_reset = (flush == 1) & (pending_reads_counter == 0);  // resetting the length counter will trigger the done condition

  // need to adjust the read throttling based on unaligned accesses being enabled, when enabled there is an additional stage of pipelining before the FIFO that needs to be accounted for
  generate
    if (UNALIGNED_ACCESSES_ENABLE == 1)
    begin
      assign too_many_pending_reads = (({fifo_full,fifo_used} + pending_reads_counter) > (FIFO_DEPTH - (maximum_burst_count * 3)));  // making sure a full burst can be posted, using 3x maximum_burst_count since the read signal is pipelined and there is a pipeline stage in the MM to ST adapter and so this signal will be late using maximum_burst_count alone
    end
    else
    begin
      assign too_many_pending_reads = (({fifo_full,fifo_used} + pending_reads_counter) > (FIFO_DEPTH - (maximum_burst_count * 2)));  // making sure a full burst can be posted, using 2x maximum_burst_count since the read signal is pipelined and so this signal will be late using maximum_burst_count alone
    end
  endgenerate
  
  assign read_complete = (master_read == 1) & (master_waitrequest == 0);
  assign address_increment_enable = read_complete;

  assign master_byteenable = {BYTE_ENABLE_WIDTH{1'b1}};  // master always asserts all byte enables and filters the data as it comes in (may lead to destructive reads in some cases)

  generate if (DATA_WIDTH > 8)
  begin
    assign master_address = address_counter & { {(ADDRESS_WIDTH-BYTE_ENABLE_WIDTH_LOG2){1'b1}}, {BYTE_ENABLE_WIDTH_LOG2{1'b0}} };  // masking LSBs (byte offsets) since the address counter might not be aligned for the first transfer
  end
  else
  begin
    assign master_address = address_counter;  // don't need to mask any bits as the address will only advance one byte at a time
  end
  endgenerate

  assign master_read = master_read_reg & (done == 0);  // need to mask the read with done so that it doesn't issue one extra read at the end
	
  assign all_reads_returned = (done == 1) & (pending_reads_counter == 0);
  assign all_reads_returned_strobe = (all_reads_returned == 1) & (all_reads_returned_d1 == 0);

  // for now the done and early done strobes are the same.  Both will be triggered when the last data returns
  generate
  if (UNALIGNED_ACCESSES_ENABLE == 1)  // need to use the delayed strobe since there are two stages of pipelining in the MM to ST adapter
  begin
    assign src_response_data = {{252{1'b0}}, all_reads_returned_strobe_d2, done_strobe, stopped, flush};  // 252 zeros: done strobe: early done strobe: stop state: reset delayed
  end
  else
  begin
    assign src_response_data = {{252{1'b0}}, all_reads_returned_strobe, done_strobe, stopped, flush};  // 252 zeros: done strobe: early done strobe: stop state: reset delayed
  end
  endgenerate

  
  assign set_src_response_valid = (UNALIGNED_ACCESSES_ENABLE == 1)? all_reads_returned_strobe_d2 : // all the reads have returned with MM to ST adapter latency taken into consideration
                                  (early_done_enable_d1 == 1)? done_strobe : all_reads_returned_strobe; // when early done is enabled then the done strobe is sufficient to trigger the next command can enter, otherwise need to wait for the pending reads to return
/****************************** END CONTROL AND COMBINATIONAL SIGNALS *************************************/
endmodule
