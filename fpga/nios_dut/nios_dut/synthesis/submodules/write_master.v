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
  This write master module is responsible for taking in streaming data and
  writing the contents out to memory.  It is controlled by a streaming
  sink port called the 'command port'.  Any information that must be communicated
  back to a host such as an error in transfer is made available by the
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

  2.0 Added support for unaligned accesses, stride, and streaming.

  2.1 Fixed control logic and removed the early termination enable logic (it's
      always on now so for packet transfers make sure the length register is
      programmed accordingly.

  2.2 Added burst support.

  2.3 Added additional conditional code for 8-bit case to avoid synthesis issues.
  
  2.4 Corrected burst bug that prevented full bursts from being presented to the
      fabric.  Corrected the stop/reset logic to ensure masters can be stopped
      or reset while idle.
      
  2.5 Corrected a packet problem where EOP wasn't qualified by ready and valid.
      Added 64-bit addressing.

*/


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 


module write_master (
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
  snk_data,
  snk_valid,
  snk_ready,
  snk_sop,
  snk_eop,
  snk_empty,
  snk_error,

  // data path master port
  master_address,
  master_write,
  master_byteenable,
  master_writedata,
  master_waitrequest,
  master_burstcount
);

  parameter UNALIGNED_ACCESSES_ENABLE = 0;        // when enabled allows transfers to begin from off word boundaries
  parameter ONLY_FULL_ACCESS_ENABLE = 0;          // when enabled allows transfers to end with partial access, master achieve a much higher fmax when this is enabled
  parameter STRIDE_ENABLE = 0;                    // stride support can only be enabled when unaligned accesses is disabled
  parameter STRIDE_WIDTH = 1;                     // when stride support is enabled this value controls the rate in which the address increases (in words), the stride width + log2(byte enable width) + 1 cannot exceed address width               
  parameter PACKET_ENABLE = 0;
  parameter ERROR_ENABLE = 0;
  parameter ERROR_WIDTH = 8;                      // must be between 1-8, this will only be enabled in the GUI when error enable is turned on
  parameter DATA_WIDTH = 32;
  parameter BYTE_ENABLE_WIDTH = 4;                // set by the .tcl file (hidden in GUI)
  parameter BYTE_ENABLE_WIDTH_LOG2 = 2;           // set by the .tcl file (hidden in GUI)
  parameter ADDRESS_WIDTH = 32;                   // set in the .tcl file (hidden in GUI) by the address span of the master
  parameter LENGTH_WIDTH = 32;                    // GUI setting with warning if ADDRESS_WIDTH < LENGTH_WIDTH (waste of logic for the length counter)
  parameter ACTUAL_BYTES_TRANSFERRED_WIDTH = 32;  // GUI setting which can only be set when packet support is enabled (otherwise it'll be set to 32).  A warning will be issued if overrun protection is enabled and this setting is less than the length width.
  parameter FIFO_DEPTH = 32;
  parameter FIFO_DEPTH_LOG2 = 5;                  // set by the .tcl file (hidden in GUI)
  parameter FIFO_SPEED_OPTIMIZATION = 1;          // set by the .tcl file (hidden in GUI)  The default will be on since it only impacts the latency of the entire transfer by 1 clock cycle and adds very little additional logic.
  parameter SYMBOL_WIDTH = 8;                     // set by the .tcl file (hidden in GUI)
  parameter NUMBER_OF_SYMBOLS = 4;                // set by the .tcl file (hidden in GUI)
  parameter NUMBER_OF_SYMBOLS_LOG2 = 2;           // set by the .tcl file (hidden in GUI)
  parameter BURST_ENABLE = 0;
  parameter MAX_BURST_COUNT = 2;                  // must be a power of 2, when BURST_ENABLE = 0 set the maximum burst count to 1 (automatically done in the .tcl file)
  parameter MAX_BURST_COUNT_WIDTH = 2;            // set by the .tcl file (hidden in GUI) = log2(MAX_BURST_COUNT) + 1
  parameter PROGRAMMABLE_BURST_ENABLE = 0;        // when enabled the user must set the burst count, if 0 is set then the value MAX_BURST_COUNT will be used instead
  parameter BURST_WRAPPING_SUPPORT = 1;           // will only be used when bursting is enabled.  This cannot be enabled with programmable burst capabilities.  Enabling it will make sure the master gets back into burst alignment (data width in bytes * maximum burst count alignment)

  localparam FIFO_USE_MEMORY = 1;                 // set to 0 to use LEs instead, not exposed since FPGAs have a lot of memory these days
  localparam BIG_ENDIAN_ACCESS = 0;               // hiding this since it can blow your foot off if you are not careful and it's not tested.  It's big endian with respect to the write master width and not necessarily to the width of the data type used by a host CPU.
  // handy mask for seperating the word address from the byte address bits, so for 32 bit masters this mask is 0x3, for 64 bit masters it'll be 0x7
  localparam LSB_MASK = {BYTE_ENABLE_WIDTH_LOG2{1'b1}};
  //need to buffer the empty, eop, sop, and error bits.  If these are not needed then the logic will be synthesized away
  localparam FIFO_WIDTH = (DATA_WIDTH + 2 + NUMBER_OF_SYMBOLS_LOG2 + ERROR_WIDTH);  // data, sop, eop, empty, and error bits
  localparam ADDRESS_INCREMENT_WIDTH = (BYTE_ENABLE_WIDTH_LOG2 + MAX_BURST_COUNT_WIDTH + STRIDE_WIDTH);
  localparam FIXED_STRIDE = 1'b1;  // when stride isn't supported this will be the stride value used (i.e. sequential incrementing of the address)


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

  // data path sink port
  input [DATA_WIDTH-1:0] snk_data;
  input snk_valid;
  output wire snk_ready;
  input snk_sop;
  input snk_eop;
  input [NUMBER_OF_SYMBOLS_LOG2-1:0] snk_empty;
  input [ERROR_WIDTH-1:0] snk_error;

  // master inputs and outputs
  input master_waitrequest;
  output wire [ADDRESS_WIDTH-1:0] master_address;
  output wire master_write;
  output wire [BYTE_ENABLE_WIDTH-1:0] master_byteenable;
  output wire [DATA_WIDTH-1:0] master_writedata;
  output wire [MAX_BURST_COUNT_WIDTH-1:0] master_burstcount;


  // internal wires and registers
  wire [63:0] descriptor_address;
  wire [31:0] descriptor_length;
  wire [15:0] descriptor_stride;
  wire descriptor_end_on_eop_enable;
  wire [7:0] descriptor_programmable_burst_count;
  reg [ADDRESS_WIDTH-1:0] address_counter;
  wire [ADDRESS_WIDTH-1:0] address;  // unfiltered version of master_address
  wire write;  // unfiltered version of master_write
  reg [LENGTH_WIDTH-1:0] length_counter;
  reg [STRIDE_WIDTH-1:0] stride_d1;
  wire [STRIDE_WIDTH-1:0] stride_amount;  // either set to be stride_d1 or hardcoded to 1 depending on the parameterization
  reg descriptor_end_on_eop_enable_d1;
  reg [MAX_BURST_COUNT_WIDTH-1:0] programmable_burst_count_d1;
  wire [MAX_BURST_COUNT_WIDTH-1:0] maximum_burst_count;
  reg [BYTE_ENABLE_WIDTH_LOG2-1:0] start_byte_address;  // used to determine how far out of alignement the master started
  reg first_access;  // used to prevent extra writes when the unaligned access starts and ends during the same write
  wire first_word_boundary_not_reached;   // set when the first access doesn't reach the next word boundary
  reg first_word_boundary_not_reached_d1;
  wire increment_address;  // enable the address incrementing
  wire [ADDRESS_INCREMENT_WIDTH-1:0] address_increment;  // amount of bytes to increment the address
  wire [ADDRESS_INCREMENT_WIDTH-1:0] bytes_to_transfer;
  wire short_first_access_enable;           // when starting unaligned and the amount of data to transfer reaches the next word boundary
  wire short_last_access_enable;            // when address is aligned (can be an unaligned buffer transfer) but the amount of data doesn't reach the next word boundary 
  wire short_first_and_last_access_enable;  // when starting unaligned and the amount of data to transfer doesn't reach the next word boundary
  wire [ADDRESS_INCREMENT_WIDTH-1:0] short_first_access_size;
  wire [ADDRESS_INCREMENT_WIDTH-1:0] short_last_access_size;
  wire [ADDRESS_INCREMENT_WIDTH-1:0] short_first_and_last_access_size;
  reg [ADDRESS_INCREMENT_WIDTH-1:0] bytes_to_transfer_mux;  
  wire [FIFO_WIDTH-1:0] fifo_write_data;
  wire [FIFO_WIDTH-1:0] fifo_read_data;
  wire [FIFO_DEPTH_LOG2-1:0] fifo_used;
  wire fifo_write;
  wire fifo_read;
  wire fifo_empty;
  wire fifo_full;
  wire [DATA_WIDTH-1:0] fifo_read_data_rearranged;  // if big endian support is enabled then this signal has the FIFO output byte lanes reversed
  wire go;
  wire done;
  reg done_d1;
  wire done_strobe;
  wire [DATA_WIDTH-1:0] buffered_data;
  wire [NUMBER_OF_SYMBOLS_LOG2-1:0] buffered_empty;
  wire buffered_eop;
  wire buffered_sop;  // not wired to anything so synthesized away, included for debug purposes
  wire [ERROR_WIDTH-1:0] buffered_error;
  wire length_sync_reset;  // syncronous reset for the length counter for eop support
  reg [ACTUAL_BYTES_TRANSFERRED_WIDTH-1:0] actual_bytes_transferred_counter;  // width will be in the range of 1-32
  wire [31:0] response_actual_bytes_transferred;
  wire early_termination;
  reg early_termination_d1;
  wire eop_enable;
  reg [ERROR_WIDTH-1:0] error;    // SRFF so that we don't loose any errors if EOP doesn't arrive right away
  wire [7:0] response_error;      // need to pad upper error bits with zeros if they are not present at the data streaming port
  wire sw_stop_in;
  wire sw_reset_in;
  reg stopped;                    // SRFF to make sure we don't attempt to stop in the middle of a transfer
  reg reset_taken;                // FF to make sure we don't attempt to reset the master in the middle of a transfer
  wire reset_taken_from_write_burst_control;  // in the middle of a burst greater than one, the burst control block will assert this signal after the burst copmletes, 'reset_taken' will use this signal
  wire stopped_from_write_burst_control;      // in the middle of a burst greater than one, the burst control block will assert this signal after the burst completes, 'stopped' will use this signal
  wire stop_state;
  wire reset_delayed;
  wire write_complete;            // handy signal for determining when a write has occured and completed
  wire write_stall_from_byte_enable_generator;  // partial word access occuring which might take multiple write cycles to complete (or waitrequest has been asserted)
  wire write_stall_from_write_burst_control;    // when there isn't enough data buffered to start a burst this signal will be asserted
  wire [BYTE_ENABLE_WIDTH-1:0] byteenable_masks [0:BYTE_ENABLE_WIDTH-1];  // a bunch of masks that will be provided to unsupported_byteenable
  wire [BYTE_ENABLE_WIDTH-1:0] unsupported_byteenable;  // input into the byte enable generation block which will take the unsupported byte enable and chop it up into supported transfers
  wire [BYTE_ENABLE_WIDTH-1:0] supported_byteenable;  // output from the byte enable generation block
  wire extra_write;  // when asserted master_write will be asserted but the FIFO will not be popped since it will not contain any more data for the transfer
  wire st_to_mm_adapter_enable;
  wire [BYTE_ENABLE_WIDTH_LOG2:0] packet_beat_size;  // number of bytes coming in from the data stream when packet support is enabled
  wire [BYTE_ENABLE_WIDTH_LOG2:0] packet_bytes_buffered;
  reg [BYTE_ENABLE_WIDTH_LOG2:0] packet_bytes_buffered_d1;  // represents the number of bytes buffered in the ST to MM adapter (only applicable for unaligned accesses)
  reg eop_seen;  // when the beat containing EOP has been popped from the fifo this bit will be set, it will be reset when done is asserted.  It is used to determine if an extra write must occur (unaligned accesses only)


/********************************************* REGISTERS ****************************************************************************************/
  // registering the stride control bit
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      stride_d1 <= 0;
    end
    else if (go == 1)
    begin
      stride_d1 <= descriptor_stride[STRIDE_WIDTH-1:0];
    end
  end


  // registering the end on eop bit (will be optimized away if packet support is disabled)
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      descriptor_end_on_eop_enable_d1 <= 1'b0;
    end
    else if (go == 1)
    begin
      descriptor_end_on_eop_enable_d1 <= descriptor_end_on_eop_enable;
    end
  end


  // registering the programmable burst count (will be optimized away if this support is disabled)
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      programmable_burst_count_d1 <= 0;
    end
    else if (go == 1)
    begin
      programmable_burst_count_d1 <= ((descriptor_programmable_burst_count == 0) | (descriptor_programmable_burst_count > 128)) ? MAX_BURST_COUNT : descriptor_programmable_burst_count;
    end
  end


  // master address increment counter
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
      else if (increment_address == 1)
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


  // first_access will be asserted only for the first write of a transaction, this will be used to filter 'extra_write' for unaligned accesses
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      first_access <= 0;
    end
    else
    begin
      if (go == 1)
      begin
        first_access <= 1;
      end
      else if ((first_access == 1) & (increment_address == 1))
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
      if (length_sync_reset == 1)  // when packet support is enabled the length register might roll over so this sync reset will prevent that from happening (it's also used when a soft reset is triggered)
      begin
        length_counter <= 0;  // when EOP arrives need to stop counting, length=0 is the done condition
      end
      else if (go == 1)
      begin
        length_counter <= descriptor_length[LENGTH_WIDTH-1:0];
      end
      else if (increment_address == 1)
      begin
        length_counter <= length_counter - bytes_to_transfer;  // not using address_increment because stride might be enabled
      end
    end
  end


  // master actual bytes transferred logic, this will only be used when packet support is enabled, otherwise the value will be 0
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      actual_bytes_transferred_counter <= 0;
    end
    else
    begin
      if ((go == 1) | (reset_taken == 1))
      begin
        actual_bytes_transferred_counter <= 0;
      end
      else if(increment_address == 1)
      begin
        actual_bytes_transferred_counter <= actual_bytes_transferred_counter + bytes_to_transfer;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      done_d1 <= 1;     // out of reset the master needs to be 'done' so that the done_strobe doesn't fire
    end
    else
    begin
      done_d1 <= done;
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      early_termination_d1 <= 0;
    end
    else
    begin
      early_termination_d1 <= early_termination;
    end
  end


  generate
  genvar l;
  for(l = 0; l < ERROR_WIDTH; l = l + 1)
  begin: error_SRFF
    always @ (posedge clk or posedge reset)
    begin
      if (reset)
      begin
        error[l] <= 0;
      end
      else
      begin
        if ((go == 1) | (reset_taken == 1))
        begin
          error[l] <= 0;
        end
        else if ((buffered_error[l] == 1) & (done == 0))
        begin
          error[l] <= 1;
        end
      end
    end
  end
  endgenerate


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
      else if (((done == 1) & (src_response_valid == 0)) | (reset_taken == 1))  // need to make sure the response is popped before accepting more commands
      begin
        snk_command_ready <= 1;
      end
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
      if (reset_taken == 1)
      begin
        src_response_valid <= 0;
      end
      else if (done_strobe == 1)
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
      stopped <= 0;
    end
    else
    begin
      if ((sw_stop_in == 0) | (reset_taken == 1))
      begin
        stopped <= 0;
      end
      else if ((sw_stop_in == 1) & (((write_complete == 1) & (stopped_from_write_burst_control == 1)) | ((snk_command_ready == 1) | (master_write == 0))))
      begin
        stopped <= 1;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      reset_taken <= 0;
    end
    else
    begin
      reset_taken <= (sw_reset_in == 1) & (((write_complete == 1) & (reset_taken_from_write_burst_control == 1)) | ((snk_command_ready == 1) | (master_write == 0)));
    end
  end


  // eop_seen will be set when the last beat of a packet transfer has been popped from the fifo for ST to MM block flushing purposes (extra write)
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      eop_seen <= 0;
    end
    else
    begin
      if (done == 1)
      begin
        eop_seen <= 0;
      end
      else if ((buffered_eop == 1) & (write_complete == 1))
      begin
        eop_seen <= 1;
      end
    end
  end


  // when unaligned accesses are enabled packet_bytes_buffered_d1 is the number of bytes buffered in the ST to MM block from the previous beat
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      packet_bytes_buffered_d1 <= 0;
    end
    else
    begin
      if (go == 1)
      begin
        packet_bytes_buffered_d1 <= 0;
      end
      else if (write_complete == 1)
      begin
        packet_bytes_buffered_d1 <= packet_bytes_buffered; 
      end
    end
  end
/********************************************* END REGISTERS ************************************************************************************/




/********************************************* MODULE INSTANTIATIONS ****************************************************************************/
  /* buffered sop, eop, empty, error, data (in that order).  sop, eop, and empty are only used when packet support is enabled,
     likewise error is only used when error support is enabled */
  scfifo the_st_to_master_fifo (
    .aclr (reset),
    .sclr (reset_taken),
    .clock (clk),
    .data (fifo_write_data),
    .full (fifo_full),
    .empty (fifo_empty),
    .q (fifo_read_data),
    .rdreq (fifo_read),
    .usedw (fifo_used),
    .wrreq (fifo_write)
  );
  defparam the_st_to_master_fifo.lpm_width = FIFO_WIDTH;
  defparam the_st_to_master_fifo.lpm_widthu = FIFO_DEPTH_LOG2;
  defparam the_st_to_master_fifo.lpm_numwords = FIFO_DEPTH;
  defparam the_st_to_master_fifo.lpm_showahead = "ON";  // slower but doesn't require complex control logic to time with waitrequest
  defparam the_st_to_master_fifo.use_eab = (FIFO_USE_MEMORY == 1)? "ON" : "OFF";
  defparam the_st_to_master_fifo.add_ram_output_register = (FIFO_SPEED_OPTIMIZATION == 1)? "ON" : "OFF";
  defparam the_st_to_master_fifo.underflow_checking = "OFF";
  defparam the_st_to_master_fifo.overflow_checking = "OFF";


  /* This module will barrelshift the data from the FIFO when unaligned accesses is enabled (we are using
     part of the FIFO word when off boundary).  When unaligned accesses is disabled then the data passes
     as wires.  The byte enable generator might require multiple cycles to perform partial accesses so a
     'stall' bit is used (triggers a stall like waitrequest)
   */
  ST_to_MM_Adapter the_ST_to_MM_Adapter (
    .clk (clk),
    .reset (reset),
    .enable (st_to_mm_adapter_enable),
    .address (descriptor_address[ADDRESS_WIDTH-1:0]),
    .start (go),
    .waitrequest (master_waitrequest),
    .stall (write_stall_from_byte_enable_generator | write_stall_from_write_burst_control),
    .write_data (master_writedata),
    .fifo_data (buffered_data),
    .fifo_empty (fifo_empty),
    .fifo_readack (fifo_read)
  );
  defparam the_ST_to_MM_Adapter.DATA_WIDTH = DATA_WIDTH;
  defparam the_ST_to_MM_Adapter.BYTEENABLE_WIDTH_LOG2 = BYTE_ENABLE_WIDTH_LOG2;
  defparam the_ST_to_MM_Adapter.ADDRESS_WIDTH = ADDRESS_WIDTH;
  defparam the_ST_to_MM_Adapter.UNALIGNED_ACCESS_ENABLE = UNALIGNED_ACCESSES_ENABLE;


  /* this block is responsible for presenting the fabric with supported byte enable combinations which can
     take multiple cycles, if full word only support is enabled this block will reduce to wires during synthesis */
  byte_enable_generator the_byte_enable_generator (
    .clk (clk),
    .reset (reset),
    .write_in (write),
    .byteenable_in (unsupported_byteenable),
    .waitrequest_out (write_stall_from_byte_enable_generator),
    .byteenable_out (supported_byteenable),
    .waitrequest_in (master_waitrequest | write_stall_from_write_burst_control)
  );
  defparam the_byte_enable_generator.BYTEENABLE_WIDTH = BYTE_ENABLE_WIDTH;


  // this block will be used to drive write, address, and burstcount to the fabric
  write_burst_control the_write_burst_control (
    .clk (clk),
    .reset (reset),
    .sw_reset (sw_reset_in),
	  .sw_stop (sw_stop_in),
    .length (length_counter),
    .eop_enabled (descriptor_end_on_eop_enable_d1),
    .eop (snk_eop),
    .ready (snk_ready),
    .valid (snk_valid),
    .early_termination (early_termination),
    .address_in (address),
    .write_in (write),
    .max_burst_count (maximum_burst_count),
    .write_fifo_used ({fifo_full,fifo_used}),
    .waitrequest (master_waitrequest),
    .short_first_access_enable (short_first_access_enable),
    .short_last_access_enable (short_last_access_enable),
    .short_first_and_last_access_enable (short_first_and_last_access_enable),
    .address_out (master_address),
    .write_out (master_write),  // filtered version of 'write'
    .burst_count (master_burstcount),
    .stall (write_stall_from_write_burst_control),
    .reset_taken (reset_taken_from_write_burst_control),
	.stopped (stopped_from_write_burst_control)
  );
  defparam the_write_burst_control.BURST_ENABLE = BURST_ENABLE;
  defparam the_write_burst_control.BURST_COUNT_WIDTH = MAX_BURST_COUNT_WIDTH;
  defparam the_write_burst_control.WORD_SIZE = BYTE_ENABLE_WIDTH;
  defparam the_write_burst_control.WORD_SIZE_LOG2 = (DATA_WIDTH == 8)? 0 : BYTE_ENABLE_WIDTH_LOG2;  // need to make sure log2(word size) is 0 instead of 1 here when the data width is 8 bits
  defparam the_write_burst_control.ADDRESS_WIDTH = ADDRESS_WIDTH;
  defparam the_write_burst_control.LENGTH_WIDTH = LENGTH_WIDTH;
  defparam the_write_burst_control.WRITE_FIFO_USED_WIDTH = FIFO_DEPTH_LOG2;
  defparam the_write_burst_control.BURST_WRAPPING_SUPPORT = BURST_WRAPPING_SUPPORT;

/********************************************* END MODULE INSTANTIATIONS ************************************************************************/





/********************************************* CONTROL AND COMBINATIONAL SIGNALS ****************************************************************/
  // breakout the descriptor information into more manageable names
  assign descriptor_address = {snk_command_data[123:92], snk_command_data[31:0]};  // 64-bit addressing support
  assign descriptor_length = snk_command_data[63:32];
  assign descriptor_programmable_burst_count = snk_command_data[75:68];
  assign descriptor_stride = snk_command_data[91:76];
  assign descriptor_end_on_eop_enable = snk_command_data[64];
  assign sw_stop_in = snk_command_data[66];
  assign sw_reset_in = snk_command_data[67];


  assign stride_amount = (STRIDE_ENABLE == 1)? stride_d1[STRIDE_WIDTH-1:0] : FIXED_STRIDE;  // hardcoding to FIXED_STRIDE when stride capabilities are disabled
  assign maximum_burst_count = (PROGRAMMABLE_BURST_ENABLE == 1)? programmable_burst_count_d1 : MAX_BURST_COUNT;
  assign eop_enable = (PACKET_ENABLE == 1)? descriptor_end_on_eop_enable_d1 : 1'b0;  // no eop or early termination support when packet support is disabled
  assign done_strobe = (done == 1) & (done_d1 == 0) & (reset_taken == 0);  // set_done asserts the done register so this strobe fires when the last write completes
  assign response_error = (ERROR_ENABLE == 1)? error : 8'b00000000;
  assign response_actual_bytes_transferred = (PACKET_ENABLE == 1)? actual_bytes_transferred_counter : 32'h00000000;


  // transfer size amounts for special cases (starting unaligned, ending with a partial word, starting unaligned and ending with a partial word on the same write)
  assign short_first_access_size = BYTE_ENABLE_WIDTH - start_byte_address;
  assign short_last_access_size = (eop_enable == 1)? (packet_beat_size + packet_bytes_buffered_d1) : (length_counter & LSB_MASK);
  assign short_first_and_last_access_size = (eop_enable == 1)? (BYTE_ENABLE_WIDTH - buffered_empty) : (length_counter & LSB_MASK);

  /* special case transfer enables and counter increment values (address_counter, length_counter, and actual_bytes_transferred)
     short_first_access_enable is for transfers that start aligned but reach the next word boundary
     short_last_access_enable is for transfers that are not the first transfer but don't end with on a word boundary
     short_first_and_last_access_enable is for transfers that start and end with a single transfer and don't end on a word boundary (may or may not be aligned)
  */
  generate
    if (UNALIGNED_ACCESSES_ENABLE == 1)
    begin
      // all three enables are mutually exclusive to provide one-hot encoding for the bytes to transfer mux
      assign short_first_access_enable = (start_byte_address != 0) & (first_access == 1) & ((eop_enable == 1)? ((start_byte_address + BYTE_ENABLE_WIDTH - buffered_empty) >= BYTE_ENABLE_WIDTH) : (first_word_boundary_not_reached_d1 == 0));
      assign short_last_access_enable = (first_access == 0) & ((eop_enable == 1)? ((packet_beat_size + packet_bytes_buffered_d1) < BYTE_ENABLE_WIDTH): (length_counter < BYTE_ENABLE_WIDTH));
      assign short_first_and_last_access_enable = (first_access == 1) & ((eop_enable == 1)? ((start_byte_address + BYTE_ENABLE_WIDTH - buffered_empty) < BYTE_ENABLE_WIDTH) : (first_word_boundary_not_reached_d1 == 1));
      assign bytes_to_transfer = bytes_to_transfer_mux;
      assign address_increment = bytes_to_transfer_mux;  // can't use stride when unaligned accesses are enabled
    end
    else if (ONLY_FULL_ACCESS_ENABLE == 1)
    begin 
      assign short_first_access_enable = 0;
      assign short_last_access_enable = 0;
      assign short_first_and_last_access_enable = 0;
      assign bytes_to_transfer = BYTE_ENABLE_WIDTH;
      if (STRIDE_ENABLE == 1)
      begin
        assign address_increment = BYTE_ENABLE_WIDTH * stride_amount;  // the byte address portion of the address_counter is grounded to make sure the address presented to the fabric is aligned
      end
      else
      begin
        assign address_increment = BYTE_ENABLE_WIDTH;  // the byte address portion of the address_counter is grounded to make sure the address presented to the fabric is aligned
      end
    end
    else  // must be aligned but can end with any number of bytes
    begin 
      assign short_first_access_enable = 0;
      assign short_last_access_enable = (eop_enable == 1)? (buffered_eop == 1) : (length_counter < BYTE_ENABLE_WIDTH);    // less than a word to transfer
      assign short_first_and_last_access_enable = 0;
      assign bytes_to_transfer = bytes_to_transfer_mux;
      if (STRIDE_ENABLE == 1)
      begin
        assign address_increment = BYTE_ENABLE_WIDTH * stride_amount;
      end
      else
      begin
        assign address_increment = BYTE_ENABLE_WIDTH;
      end
    end
  endgenerate

  // the control logic ensures this mux is one-hot with the fall through being the typical full word aligned access
  always @ (short_first_access_enable or short_last_access_enable or short_first_and_last_access_enable or short_first_access_size or short_last_access_size or short_first_and_last_access_size)
  begin
    case ({short_first_and_last_access_enable, short_last_access_enable, short_first_access_enable})
      3'b001: bytes_to_transfer_mux = short_first_access_size;            // unaligned and reaches the next word boundary
      3'b010: bytes_to_transfer_mux = short_last_access_size;             // aligned and does not reach the next word boundary
      3'b100: bytes_to_transfer_mux = short_first_and_last_access_size;   // unaligned and does not reach the next word boundary
      default: bytes_to_transfer_mux = BYTE_ENABLE_WIDTH;                 // aligned and reaches the next word boundary (i.e. a full word transfer)
    endcase
  end


  // Avalon-ST is network order (a.k.a. big endian) so we need to reverse the symbols before jamming them into the FIFO, changing the symbol width to something other than 8 might break something...
  generate
    genvar i;
    for(i = 0; i < DATA_WIDTH; i = i + SYMBOL_WIDTH)  // the data width is always a multiple of the symbol width
    begin: symbol_swap
      assign fifo_write_data[i +SYMBOL_WIDTH -1: i] = snk_data[DATA_WIDTH -i -1: DATA_WIDTH -i - SYMBOL_WIDTH];
    end
  endgenerate

  // sticking the error, empty, eop, and eop bits at the top of the FIFO write data, flooring empty to zero when eop is not asserted (empty is only valid on eop cycles)
  assign fifo_write_data[FIFO_WIDTH-1:DATA_WIDTH] = {snk_error, (snk_eop == 1)? snk_empty:{NUMBER_OF_SYMBOLS_LOG2{1'b0}}, snk_sop, snk_eop};


  // swap the bytes if big endian is enabled (remember that this isn't tested so use at your own risk and make sure you understand the software impact this has)
  generate
  if(BIG_ENDIAN_ACCESS == 1)
  begin
    genvar j;
    for(j=0; j < DATA_WIDTH; j = j + 8)
    begin: byte_swap
      assign fifo_read_data_rearranged[j +8 -1: j] = fifo_read_data[DATA_WIDTH -j -1: DATA_WIDTH -j - 8];
      assign master_byteenable[j/8] = supported_byteenable[(DATA_WIDTH -j -1)/8];
    end
  end
  else
  begin
    assign fifo_read_data_rearranged = fifo_read_data[DATA_WIDTH-1:0];  // little endian so no byte swapping necessary
    assign master_byteenable = supported_byteenable;    // dito
  end
  endgenerate

  // fifo read data is in the format of {error, empty, sop, eop, data} with the following widths {ERROR_WIDTH, NUMBER_OF_SYMBOLS_LOG2, 1, 1, DATA_WIDTH}
  assign buffered_data = fifo_read_data_rearranged;
  assign buffered_error = fifo_read_data[DATA_WIDTH +2 +NUMBER_OF_SYMBOLS_LOG2 + ERROR_WIDTH -1: DATA_WIDTH +2 +NUMBER_OF_SYMBOLS_LOG2];


  generate
  if (PACKET_ENABLE == 1)
  begin
    assign buffered_eop = fifo_read_data[DATA_WIDTH];
    assign buffered_sop = fifo_read_data[DATA_WIDTH +1];
    if (ONLY_FULL_ACCESS_ENABLE == 1)
    begin
      assign buffered_empty = 0;  // ignore the empty signal and assume it was a full beat
    end
    else
    begin
      assign buffered_empty = fifo_read_data[DATA_WIDTH +2 +NUMBER_OF_SYMBOLS_LOG2 -1: DATA_WIDTH +2];  // empty is packed into the upper FIFO bits
    end
  end
  else
  begin
    assign buffered_empty = 0;
    assign buffered_eop = 0;
    assign buffered_sop = 0;
  end
  endgenerate


  /*  Generating mask bits based on the size of the transfer before the unaligned access adjustment.  This is based on the
      transfer size to determine how many byte enables would be asserted in the aligned case.  Afterwards the
      byte enables will be shifted left based on how far out of alignment the address counter is (should only happen for the
      first transfer).  If the data path is 32 bits wide then the following masks are generated:

      Transfer Size       Index           Mask
            1               0             0001
            2               1             0011
            3               2             0111
            4               3             1111

      Note that the index is just the transfer size minus one
  */
  generate if (BYTE_ENABLE_WIDTH > 1)
  begin
    genvar k;
    for (k = 0; k < BYTE_ENABLE_WIDTH; k = k + 1)
    begin: byte_enable_loop
      assign byteenable_masks[k] = { {(BYTE_ENABLE_WIDTH-k-1){1'b0}}, {(k+1){1'b1}} };  // Byte enable width - k zeros followed by k ones
    end
  end
  else
  begin
    assign byteenable_masks[0] = 1'b1;  // will be stubbed at top level
  end
  endgenerate


  /* byteenable_mask is based on an aligned access determined by the transfer size.  This value is then shifted
     to the left by the unaligned offset (first transfer only) to compensate for the unaligned offset so that the
     correct byte enables are enabled.  When the accesses are aligned then no barrelshifting is needed and when full
     accesses are used then all byte enables will be asserted always. */
  generate if (ONLY_FULL_ACCESS_ENABLE == 1)
  begin
    assign unsupported_byteenable = {BYTE_ENABLE_WIDTH{1'b1}};  // always full accesses so the byte enables are all ones
  end
  else if (UNALIGNED_ACCESSES_ENABLE == 0)
  begin
    assign unsupported_byteenable = byteenable_masks[bytes_to_transfer_mux - 1];  // aligned so no unaligned adjustment required
  end
  else  // unaligned case
  begin
    assign unsupported_byteenable = byteenable_masks[bytes_to_transfer_mux - 1] << (address_counter & LSB_MASK);  // barrelshift adjusts for unaligned start address
  end
  endgenerate


  generate if (BYTE_ENABLE_WIDTH > 1)
  begin
    assign address = address_counter & { {(ADDRESS_WIDTH-BYTE_ENABLE_WIDTH_LOG2){1'b1}}, {BYTE_ENABLE_WIDTH_LOG2{1'b0}} };  // masking LSBs (byte offsets) since the address counter might not be aligned for the first transfer
  end
  else
  begin
    assign address = address_counter;  // don't need to mask any bits as the address will only advance one byte at a time
  end
  endgenerate

  assign done = (length_counter == 0) | ((PACKET_ENABLE == 1) & (eop_enable == 1) & (eop_seen == 1) & (extra_write == 0));

  assign packet_beat_size = (eop_seen == 1) ? 0 : (BYTE_ENABLE_WIDTH - buffered_empty);  // when the eop arrives we can't add more to packet_bytes_buffered_d1
  assign packet_bytes_buffered = packet_beat_size + packet_bytes_buffered_d1 - bytes_to_transfer;

  // extra_write is only applicable when unaligned accesses are performed.  This extra access gets the remaining data buffered in the ST to MM adapter block written to memory
  assign extra_write = (UNALIGNED_ACCESSES_ENABLE == 1) & (((PACKET_ENABLE == 1) & (eop_enable == 1))? 
                       ((eop_seen == 1) & (packet_bytes_buffered_d1 != 0)) : // when packets are used if there are left over bytes buffered after eop is seen perform an extra write
                       ((first_access == 0) & (start_byte_address != 0) & (short_last_access_enable == 1) & (start_byte_address >= length_counter[BYTE_ENABLE_WIDTH_LOG2-1:0])));  // non-packet transfer and there are extra bytes buffered so performing an extra access


  assign first_word_boundary_not_reached = (descriptor_length < BYTE_ENABLE_WIDTH) &  // length is less than the word size
                                           (((descriptor_length & LSB_MASK) + (descriptor_address & LSB_MASK)) < BYTE_ENABLE_WIDTH);  // start address + length doesn't reach the next word boundary (not used for packet transfers)
  
  assign write = ((fifo_empty == 0) | (extra_write == 1)) & (done == 0) & (stopped == 0);
  assign st_to_mm_adapter_enable = (done == 0) & (extra_write == 0);

  assign write_complete = (write == 1) & (master_waitrequest == 0) & (write_stall_from_byte_enable_generator == 0) & (write_stall_from_write_burst_control == 0);  // writing still occuring and no reasons to prevent the write cycle from completing
  assign increment_address = ((write == 1) & (write_complete == 1)) & (stopped == 0);
  assign go = (snk_command_valid == 1) & (snk_command_ready == 1);  // go with be one cycle since done will be set to 0 on the next cycle (length will be non-zero)

  assign snk_ready = (fifo_full == 0) & // need to make sure more streaming data doesn't come in when the FIFO is full
                     (((PACKET_ENABLE == 1) & (snk_sop == 1) & (fifo_empty == 0)) != 1);  // need to make sure that only one packet is buffered at any given time (sop will continue to be asserted until the buffer is written out)

  assign length_sync_reset = (((reset_taken == 1) | (early_termination_d1 == 1)) & (done == 0)) | (done_strobe == 1); // abrupt stop cases or packet transfer just completed (otherwise the length register will reach 0 by itself)


  assign fifo_write = (snk_ready == 1) & (snk_valid == 1);

  assign early_termination = (eop_enable == 1) & (write_complete == 1) & (length_counter < bytes_to_transfer);  // packet transfer and the length counter is about to roll over so stop transfering
  
  assign stop_state = stopped;
  assign reset_delayed = (reset_taken == 0) & (sw_reset_in == 1);
  assign src_response_data = {{212{1'b0}}, done_strobe, early_termination_d1, response_error, stop_state, reset_delayed, response_actual_bytes_transferred};
/********************************************* END CONTROL AND COMBINATIONAL SIGNALS ************************************************************/

endmodule
