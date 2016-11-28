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
  This block is used to breakout the 256 bit streaming ports to and from the write master.
  The information sent through the streaming ports is a bundle of wires and buses so it's
  fairly inconvenient to constantly refer to them by their position amungst the 256 lines.
  This block also provides a layer of abstraction since the descriptor buffers block has
  no clue what format the descriptors are in except that the 'go' bit is written to.  This
  means that using this block you could move descriptor information around without affecting
  the top level dispatcher logic.
  
  
  1.0  06/29/2009 - First version of this block of wires
  
  1.1  02/15/2011 - Added read_early_done_enable to the wire breakout
  
  1.2  11/15/2012 - Added in an additional 32 bits of address for extended descriptors
  
*/


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 


module read_signal_breakout (
  read_command_data_in,     // descriptor from the read FIFO
  read_command_data_out,  // reformated descriptor to the read master

  // breakout of command information
  read_address,
  read_length,
  read_transmit_channel,
  read_generate_sop,
  read_generate_eop,
  read_park,
  read_transfer_complete_IRQ_mask,
  read_burst_count,      // when 'ENHANCED_FEATURES' is 0 this will be driven to ground
  read_stride,           // when 'ENHANCED_FEATURES' is 0 this will be driven to ground
  read_sequence_number,  // when 'ENHANCED_FEATURES' is 0 this will be driven to ground
  read_transmit_error,
  read_early_done_enable,

  // additional control information that needs to go out asynchronously with the command data
  read_stop,
  read_sw_reset
);

  parameter DATA_WIDTH = 256;  // 256 bits when enhanced settings are enabled otherwise 128 bits

  input [DATA_WIDTH-1:0] read_command_data_in;
  output wire [255:0] read_command_data_out;

  output wire [63:0] read_address;
  output wire [31:0] read_length;
  output wire [7:0] read_transmit_channel;
  output wire read_generate_sop;
  output wire read_generate_eop;
  output wire read_park;
  output wire read_transfer_complete_IRQ_mask;
  output wire [7:0] read_burst_count;
  output wire [15:0] read_stride;
  output wire [15:0] read_sequence_number;
  output wire [7:0] read_transmit_error;
  output wire read_early_done_enable;

  input read_stop;
  input read_sw_reset;
  
  
  assign read_address[31:0] = read_command_data_in[31:0];
  assign read_length = read_command_data_in[95:64];


  generate
    if (DATA_WIDTH == 256)
    begin
      assign read_early_done_enable = read_command_data_in[248];
      assign read_transmit_error = read_command_data_in[247:240];
      assign read_transmit_channel = read_command_data_in[231:224];
      assign read_generate_sop = read_command_data_in[232];
      assign read_generate_eop = read_command_data_in[233];
      assign read_park = read_command_data_in[234];
      assign read_transfer_complete_IRQ_mask = read_command_data_in[238];
      assign read_burst_count = read_command_data_in[119:112];
      assign read_stride = read_command_data_in[143:128];
      assign read_sequence_number = read_command_data_in[111:96];
      assign read_address[63:32] = read_command_data_in[191:160];
    end
    else
    begin
      assign read_early_done_enable = read_command_data_in[120];
      assign read_transmit_error = read_command_data_in[119:112];
      assign read_transmit_channel = read_command_data_in[103:96];
      assign read_generate_sop = read_command_data_in[104];
      assign read_generate_eop = read_command_data_in[105];
      assign read_park = read_command_data_in[106];
      assign read_transfer_complete_IRQ_mask = read_command_data_in[110];
      assign read_burst_count = 8'h00;
      assign read_stride = 16'h0000;
      assign read_sequence_number = 16'h0000;  
      assign read_address[63:32] = 32'h00000000;
    end
  endgenerate

  // big concat statement to glue all the signals back together to go out to the read master (MSBs to LSBs)
  assign read_command_data_out = {{115{1'b0}},  // zero pad the upper 115 bits
                                 read_address[63:32],
                                 read_early_done_enable,
                                 read_transmit_error,
                                 read_stride,
                                 read_burst_count,
                                 read_sw_reset,
                                 read_stop,
                                 read_generate_eop,
                                 read_generate_sop,
                                 read_transmit_channel,
                                 read_length,
                                 read_address[31:0]};

endmodule
