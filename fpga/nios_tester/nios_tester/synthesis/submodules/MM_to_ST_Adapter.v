// (C) 2001-2018 Intel Corporation. All rights reserved.
// Your use of Intel Corporation's design tools, logic functions and other 
// software and tools, and its AMPP partner logic functions, and any output 
// files from any of the foregoing (including device programming or simulation 
// files), and any associated documentation or information are expressly subject 
// to the terms and conditions of the Intel Program License Subscription 
// Agreement, Intel FPGA IP License Agreement, or other applicable 
// license agreement, including, without limitation, that your use is for the 
// sole purpose of programming logic devices manufactured by Intel and sold by 
// Intel or its authorized distributors.  Please refer to the applicable 
// agreement for further details.


/*
  This logic recieves Avalon Memory Mapped read data and translates it into
  the Avalon Streaming format.  The ST format requires all data to be packed
  until the final transfer when packet support is enabled.  As a result when
  you enable unaligned acceses the data from two sucessive reads must be
  combined to form a single word of data.  If you disable packet support
  and unaligned access support this block will synthesize into wires.
  
  This block does not provide any read throttling as it simply acts as a format
  adapter between the read master port and the read master FIFO.  All throttling
  should be provided by the read master to prevent overflow.  Since this logic
  sits on the MM side of the FIFO the bytes are in 'little endian' format and
  will get swapped around on the other side of the FIFO (symbol size can be adjusted
  there too).
  
  Revision History:
  
  1.0 Initial version
  
  2.0 Removed 'bytes_to_next_boundary' and using the address and length signals
      instead to determine how much out of alignment the master begins.

  2.1 Changed the extra last access logic to be based on the descriptor address
      and length as apposed to the counter values.  Created a new 'length_counter'
      input to determine when the last read has arrived.
*/




// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 


module MM_to_ST_Adapter (
  clk,
  reset,
  
  length,
  length_counter,
  address,
  reads_pending,
  start,

  readdata,
  readdatavalid,
  fifo_data,
  fifo_write,
  fifo_empty,
  fifo_sop,
  fifo_eop
);



  parameter DATA_WIDTH = 32;                   // 8, 16, 32, 64, 128, or 256 are valid values (if 8 is used then disable unaligned accesses and turn on full word only accesses)
  parameter LENGTH_WIDTH = 32;
  parameter ADDRESS_WIDTH = 32;
  parameter BYTE_ADDRESS_WIDTH = 2;            // log2(DATA_WIDTH/8)
  parameter READS_PENDING_WIDTH = 5;
  parameter EMPTY_WIDTH = 2;                   // log2(DATA_WIDTH/8)
  parameter PACKET_SUPPORT = 1;                // when set to 1 eop, sop, and empty will be driven, otherwise they will be grounded
  // only set one of these at a time
  parameter UNALIGNED_ACCESS_ENABLE = 1;       // when set to 1 this block will support packets and starting/ending on any boundary, do not use this if DATA_WIDTH is 8 (use 'FULL_WORD_ACCESS_ONLY')
  parameter FULL_WORD_ACCESS_ONLY = 0;         // when set to 1 this block will assume only full words are arriving (must start and stop on a word boundary).


  input clk;
  input reset;
  

  input [LENGTH_WIDTH-1:0] length;
  input [LENGTH_WIDTH-1:0] length_counter;
  input [ADDRESS_WIDTH-1:0] address;
  input [READS_PENDING_WIDTH-1:0] reads_pending;
  input start;   // one cycle strobe at the start of a transfer used to capture bytes_to_transfer


  input [DATA_WIDTH-1:0] readdata;
  input readdatavalid;
  output wire [DATA_WIDTH-1:0] fifo_data;
  output wire fifo_write;
  output wire [EMPTY_WIDTH-1:0] fifo_empty;
  output wire fifo_sop;
  output wire fifo_eop;


  // internal registers and wires  
  reg [DATA_WIDTH-1:0] readdata_d1;
  reg readdatavalid_d1;
  wire [DATA_WIDTH-1:0] data_in;            // data_in will either be readdata or a pipelined copy of readdata depending on whether unaligned access support is enabled
  wire valid_in;                            // valid in will either be readdatavalid or a pipelined copy of readdatavalid depending on whether unaligned access support is enabled
  reg valid_in_d1;
  wire [DATA_WIDTH-1:0] barrelshifter_A;    // shifted current read data
  wire [DATA_WIDTH-1:0] barrelshifter_B;
  reg [DATA_WIDTH-1:0] barrelshifter_B_d1;  // shifted previously read data
  wire [DATA_WIDTH-1:0] combined_word;  // bitwise OR between barrelshifter_A and barrelshifter_B (each has zero padding so that bytelanes don't overlap)
  wire [DATA_WIDTH-1:0] barrelshifter_input_A [0:((DATA_WIDTH/8)-1)];  // will be used to create barrelshifter_A inputs
  wire [DATA_WIDTH-1:0] barrelshifter_input_B [0:((DATA_WIDTH/8)-1)];  // will be used to create barrelshifter_B inputs
  wire extra_access_enable;
  reg extra_access;
  wire last_unaligned_fifo_write;
  reg first_access_seen;
  reg second_access_seen;
  wire first_access_seen_rising_edge;
  wire second_access_seen_rising_edge;
  reg [BYTE_ADDRESS_WIDTH-1:0] byte_address;
  reg [EMPTY_WIDTH-1:0] last_empty;  // only the last word written into the FIFO can have empty bytes
  reg start_and_end_same_cycle;  // when the amount of data to transfer is only a full word or less


  generate
    if (UNALIGNED_ACCESS_ENABLE == 1)  // unaligned so using a pipelined input
    begin
      assign data_in = readdata_d1;
      assign valid_in = readdatavalid_d1;
    end
    else
    begin
      assign data_in = readdata;       // no barrelshifters in this case so pipelining is not necessary
      assign valid_in = readdatavalid;
    end
  endgenerate


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      readdata_d1 <= 0;
    end
    else
    begin
      if (readdatavalid == 1)
      begin
        readdata_d1 <= readdata;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      readdatavalid_d1 <= 0;
      valid_in_d1 <= 0;
    end
    else
    begin
      readdatavalid_d1 <= readdatavalid;
      valid_in_d1 <= valid_in;  // used to flush the pipeline (extra fifo write) and prolong eop for one additional clock cycle
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset == 1)
    begin
      barrelshifter_B_d1 <= 0;
    end
    else
    begin
      if (valid_in == 1)
      begin
        barrelshifter_B_d1 <= barrelshifter_B;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      first_access_seen <= 0;
    end
    else
    begin
      if (start == 1)
      begin
        first_access_seen <= 0;
      end
      else if (valid_in == 1)
      begin
        first_access_seen <= 1;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      second_access_seen <= 0;
    end
    else
    begin
      if (start == 1)
      begin
        second_access_seen <= 0;
      end
      else if ((first_access_seen == 1) & (valid_in == 1))
      begin
        second_access_seen <= 1;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      byte_address <= 0;
    end
    else if (start == 1)
    begin
      byte_address <= address[BYTE_ADDRESS_WIDTH-1:0];
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      last_empty <= 0;
    end
    else if (start == 1)
    begin
      last_empty <= ((DATA_WIDTH/8) - length[EMPTY_WIDTH-1:0]) & {EMPTY_WIDTH{1'b1}};  // if length isn't a multiple of the word size then we'll have some empty symbols/bytes during the last fifo write
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      extra_access <= 0;
    end
    else if (start == 1)
    begin
      extra_access <= extra_access_enable;  // when set the number of reads and fifo writes are equal, otherwise there will be 1 less fifo write than reads (unaligned accesses only)
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      start_and_end_same_cycle <= 0;
    end
    else if (start == 1)
    begin
      start_and_end_same_cycle <= (length <= (DATA_WIDTH/8));
    end
  end


  /* These barrelshifters will take the unaligned data coming into this block and shift the byte lanes appropriately to form a single packed word.
     Zeros are shifted into the byte lanes that do not contain valid data for the combined word that will be buffered.  This allows both barrelshifters
     to be logically OR'ed together to form a single packed word.  Shifter A is used to shift the current read data towards the upper bytes of the
     combined word (since those are the upper addresses of the combined word).  Shifter B after the pipeline stage called 'barrelshifter_B_d1' contains
     the previously read data shifted towards the lower bytes (since those are the lower addresses of the combined word).
  */
  generate
    genvar input_offset;
    for(input_offset = 0; input_offset < (DATA_WIDTH/8); input_offset = input_offset + 1)
    begin:  barrel_shifter_inputs
      assign barrelshifter_input_A[input_offset] = data_in << (8 * ((DATA_WIDTH/8) - input_offset)); 
      assign barrelshifter_input_B[input_offset] = data_in >> (8 * input_offset);
    end
  endgenerate

  assign barrelshifter_A = barrelshifter_input_A[byte_address];   // upper portion of the packed word
  assign barrelshifter_B = barrelshifter_input_B[byte_address];   // lower portion of the packed word (will be pipelined so it will be the previous word read by the master)
  assign combined_word = (barrelshifter_A | barrelshifter_B_d1);  // barrelshifters shift in zeros so we can just OR the words together here to create a packed word



  assign first_access_seen_rising_edge = (valid_in == 1) & (first_access_seen == 0);
  assign second_access_seen_rising_edge = ((first_access_seen == 1) & (valid_in == 1)) & (second_access_seen == 0);
  assign extra_access_enable = (((DATA_WIDTH/8) - length[EMPTY_WIDTH-1:0]) & {EMPTY_WIDTH{1'b1}}) >= address[BYTE_ADDRESS_WIDTH-1:0];  // enable when empty >= byte address


  /*  Need to keep track of the last write to the FIFO so that we can fire EOP correctly as well as flush the pipeline when unaligned accesses
      is enabled.  The first read is filtered since it is considered to be only a partial word to be written into the FIFO but there are cases
      when there is extra data that is buffered in 'barrelshifter_B_d1' but the transfer is done so we need to issue an additional write.
      In general for every 'N' Avalon-MM reads 'N-1' writes to the FIFO will occur unless there is data still buffered in which one more write
      to the FIFO will immediately follow the last read.
  */
  assign last_unaligned_fifo_write = (reads_pending == 0) & (length_counter == 0) &
                                     (  ((extra_access == 0) & (valid_in == 1)) |                         // don't need a pipeline flush
                                        ((extra_access == 1) & (valid_in_d1 == 1) & (valid_in == 0))  );  // last write to flush the pipeline (need to make sure valid_in isn't asserted to make sure the last data is indeed coming since valid_in is pipelined)


  // This block should be optimized down depending on the packet support or access type settings.  In the case where packet support is off
  // and only full accesses are used this block should become zero logic elements.
  generate
  if (PACKET_SUPPORT == 1)
  begin
    if (UNALIGNED_ACCESS_ENABLE == 1)
    begin
      assign fifo_sop = (second_access_seen_rising_edge == 1) | ((start_and_end_same_cycle == 1) & (last_unaligned_fifo_write == 1));
      assign fifo_eop = last_unaligned_fifo_write;
      assign fifo_empty = (fifo_eop == 1)? last_empty : 0;  // always full accesses until the last word
    end
    else
    begin
      assign fifo_sop = first_access_seen_rising_edge;
      assign fifo_eop = (length_counter == 0) & (reads_pending == 1) & (valid_in == 1);  // not using last_unaligned_fifo_write since it's pipelined and when unaligned accesses are disabled the input is not pipelined
      if (FULL_WORD_ACCESS_ONLY == 1)
      begin
        assign fifo_empty = 0;  // full accesses so no empty symbols throughout the transfer
      end
      else
      begin
        assign fifo_empty = (fifo_eop == 1)? last_empty : 0;  // always full accesses until the last word
      end
    end
  end
  else
  begin
    assign fifo_eop = 0;
    assign fifo_sop = 0;
    assign fifo_empty = 0;
  end


  if (UNALIGNED_ACCESS_ENABLE == 1)
  begin
    assign fifo_data = combined_word;
    assign fifo_write = (first_access_seen == 1) & ((valid_in == 1) | (last_unaligned_fifo_write == 1));  // last_unaligned_fifo_write will inject an extra pulse right after the last read occurs when flushing of the pipeline is needed
  end
  else
  begin  // don't need to pipeline since the data will not go through the barrel shifters
    assign fifo_data = data_in;   // don't need to barrelshift when aligned accesses are used
    assign fifo_write = valid_in; // the number of writes to the fifo needs to always equal the number of reads from memory
  end
  endgenerate

endmodule
