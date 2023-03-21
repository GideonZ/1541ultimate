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
  This block is responsible for accepting 128/256 bit descriptors and
  buffering them in descriptor FIFOs.  Each bytelane of the descriptor
  can be written to individually and writing ot the descriptor 'go' bit
  commits the data into the FIFO.  Reading that data out of the FIFO
  occurs two cycles after the read is asserted as the FIFOs do not support
  lookahead mode.
  
  This block must keep local copies of per descriptor information like
  the optional sequence number or interrupt masks.  When parked mode
  is set in the descriptor the same will transfer multiple times when
  the descriptor FIFO only contains one descriptor (and this descriptor
  will not be popped).  Parked mode is useful for video frame buffering.



  1.0 - The on-chip memory in the FIFOs are not inferred so there may
        be some extra unused bits.  In a later Quartus II release the
        on-chip memory will be replaced with inferred memory.

  1.1 - Shifted all descriptor registers into this block (from the dispatcher
        block).  Added breakout blocks responsible for re-packing the
        information for use by each master.
        
  1.2 - Added the read_early_done_enable bit to the breakout (for debug)
  
  1.3 - Made the read_address and write_address widths 64-bit (for debug)
*/



// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 


module descriptor_buffers (
  clk,
  reset,

  writedata,
  write,
  byteenable,
  waitrequest,

  read_command_valid,
  read_command_ready,
  read_command_data,
  read_command_empty,
  read_command_full,
  read_command_used,

  write_command_valid,
  write_command_ready,
  write_command_data,
  write_command_empty,
  write_command_full,
  write_command_used,

  stop_issuing_commands,
  stop,
  sw_reset,
  sequence_number,
  transfer_complete_IRQ_mask,
  early_termination_IRQ_mask,
  error_IRQ_mask
);

  parameter MODE = 0;
  parameter DATA_WIDTH = 256;
  parameter BYTE_ENABLE_WIDTH = 32;
  parameter FIFO_DEPTH = 128;
  parameter FIFO_DEPTH_LOG2 = 7;  // top level module can figure this out
  
  input clk;
  input reset;

  input [DATA_WIDTH-1:0] writedata;
  input write;
  input [BYTE_ENABLE_WIDTH-1:0] byteenable;
  output wire waitrequest;

  output wire read_command_valid;
  input read_command_ready;
  output wire [255:0] read_command_data;
  output wire read_command_empty;
  output wire read_command_full;
  output wire [FIFO_DEPTH_LOG2:0] read_command_used;

  output wire write_command_valid;
  input write_command_ready;
  output wire [255:0] write_command_data;
  output wire write_command_empty;
  output wire write_command_full;
  output wire [FIFO_DEPTH_LOG2:0] write_command_used;

  input stop_issuing_commands;
  input stop;
  input sw_reset;
  output wire [31:0] sequence_number;
  output wire transfer_complete_IRQ_mask;
  output wire early_termination_IRQ_mask;
  output wire [7:0] error_IRQ_mask;


  /* Internal wires and registers */
  reg write_command_empty_d1;
  reg write_command_empty_d2;
  reg read_command_empty_d1;
  reg read_command_empty_d2;
  wire push_write_fifo;
  wire pop_write_fifo;
  wire push_read_fifo;
  wire pop_read_fifo;
  wire go_bit;
  wire read_park;
  wire read_park_enable;  // park is enabled when read_park is enabled and the read FIFO is empty
  wire write_park;
  wire write_park_enable;  // park is enabled when write_park is enabled and the write FIFO is empty
  wire [DATA_WIDTH-1:0] write_fifo_output;
  wire [DATA_WIDTH-1:0] read_fifo_output;
  wire [15:0] write_sequence_number;
  reg [15:0] write_sequence_number_d1;
  wire [15:0] read_sequence_number;
  reg [15:0] read_sequence_number_d1;
  wire read_transfer_complete_IRQ_mask;
  reg read_transfer_complete_IRQ_mask_d1;
  wire write_transfer_complete_IRQ_mask;
  reg write_transfer_complete_IRQ_mask_d1;
  wire write_early_termination_IRQ_mask;
  reg write_early_termination_IRQ_mask_d1;
  wire [7:0] write_error_IRQ_mask;
  reg [7:0] write_error_IRQ_mask_d1;
  wire issue_write_descriptor;  // one cycle strobe used to indicate when there is a valid write descriptor ready to be sent to the write master
  wire issue_read_descriptor;   // one cycle strobe used to indicate when there is a valid write descriptor ready to be sent to the write master
  wire write_en;   // Qualified fifo write with waitrequest to prevent fifo overflow when descriptor slave port is being back pressured 


  /* Unused signals that are provided for debug convenience */
  wire [63:0] read_address;
  wire [31:0] read_length;
  wire [7:0] read_transmit_channel;
  wire read_generate_sop;
  wire read_generate_eop;
  wire [7:0] read_burst_count;
  wire [15:0] read_stride;
  wire [7:0] read_transmit_error;
  wire read_early_done_enable;
  wire [63:0] write_address;
  wire [31:0] write_length;
  wire write_end_on_eop;
  wire [7:0] write_burst_count;
  wire [15:0] write_stride;



  /************************************************* Registers *******************************************************/
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      write_sequence_number_d1 <= 0;
      write_transfer_complete_IRQ_mask_d1 <= 0;
      write_early_termination_IRQ_mask_d1 <= 0;
      write_error_IRQ_mask_d1 <= 0;
    end
    else if (issue_write_descriptor)  // if parked mode is enabled and there are no more descriptors buffered then this will not fire when the command is sent out
    begin
      write_sequence_number_d1 <= write_sequence_number;
      write_transfer_complete_IRQ_mask_d1 <= write_transfer_complete_IRQ_mask;
      write_early_termination_IRQ_mask_d1 <= write_early_termination_IRQ_mask;
      write_error_IRQ_mask_d1 <= write_error_IRQ_mask;
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      read_sequence_number_d1 <= 0;
      read_transfer_complete_IRQ_mask_d1 <= 0;
    end
    else if (issue_read_descriptor)  // if parked mode is enabled and there are no more descriptors buffered then this will not fire when the command is sent out
    begin
      read_sequence_number_d1 <= read_sequence_number;
      read_transfer_complete_IRQ_mask_d1 <= read_transfer_complete_IRQ_mask;
    end
  end


  // need to use a delayed valid signal since the commmand buffers have two cycles of latency
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      write_command_empty_d1 <= 0;
      write_command_empty_d2 <= 0;
      read_command_empty_d1 <= 0;
      read_command_empty_d2 <= 0;
    end
    else
    begin
      write_command_empty_d1 <= write_command_empty;
      write_command_empty_d2 <= write_command_empty_d1;
      read_command_empty_d1 <= read_command_empty;
      read_command_empty_d2 <= read_command_empty_d1;
    end
  end
  /*********************************************** End Registers *****************************************************/


  /****************************************** Module Instantiations **************************************************/
  /* the write_signal_break module simply takes the output of the descriptor buffer and reformats the data
   * to be sent in the command format needed by the master command port.  If new features are added to the
   * descriptor format then add it to this block.  This block also provides the descriptor information
   * using a naming convention isn't of bit indexes in a 256 bit wide command signal.
   */
  write_signal_breakout the_write_signal_breakout (
    .write_command_data_in (write_fifo_output),
    .write_command_data_out (write_command_data),
    .write_address (write_address),
    .write_length (write_length),
    .write_park (write_park),
    .write_end_on_eop (write_end_on_eop),
    .write_transfer_complete_IRQ_mask (write_transfer_complete_IRQ_mask),
    .write_early_termination_IRQ_mask (write_early_termination_IRQ_mask),
    .write_error_IRQ_mask (write_error_IRQ_mask),
    .write_burst_count (write_burst_count),
    .write_stride (write_stride),
    .write_sequence_number (write_sequence_number),
    .write_stop (stop),
    .write_sw_reset (sw_reset)
  );
  defparam the_write_signal_breakout.DATA_WIDTH = DATA_WIDTH;


  /* the read_signal_break module simply takes the output of the descriptor buffer and reformats the data
   * to be sent in the command format needed by the master command port.  If new features are added to the
   * descriptor format then add it to this block.  This block also provides the descriptor information
   * using a naming convention isn't of bit indexes in a 256 bit wide command signal.
   */
  read_signal_breakout the_read_signal_breakout (
    .read_command_data_in (read_fifo_output),
    .read_command_data_out (read_command_data),
    .read_address (read_address),
    .read_length (read_length),
    .read_transmit_channel (read_transmit_channel),
    .read_generate_sop (read_generate_sop),
    .read_generate_eop (read_generate_eop),
    .read_park (read_park),
    .read_transfer_complete_IRQ_mask (read_transfer_complete_IRQ_mask),
    .read_burst_count (read_burst_count),
    .read_stride (read_stride),
    .read_sequence_number (read_sequence_number),
    .read_transmit_error (read_transmit_error),
    .read_early_done_enable (read_early_done_enable),
    .read_stop (stop),
    .read_sw_reset (sw_reset)
  );
  defparam the_read_signal_breakout.DATA_WIDTH = DATA_WIDTH;


  // Descriptor FIFO allows for each byte lane to be written to and the data is not committed to the FIFO until the 'push' signal is asserted.
  // This differs from scfifo which commits the data any time the write signal is asserted.
  fifo_with_byteenables the_read_command_FIFO (
    .clk (clk),
    .areset (reset),
    .sreset (sw_reset),
    .write_data (writedata),
    .write_byteenables (byteenable),
    .write (write_en),
    .push (push_read_fifo),
    .read_data (read_fifo_output),
    .pop (pop_read_fifo),
    .used (read_command_used),  // this is a 'true used' signal with the full bit accounted for
    .full (read_command_full),
    .empty (read_command_empty)
  );
  defparam the_read_command_FIFO.DATA_WIDTH = DATA_WIDTH;  // we are not actually going to use all these bits and byte lanes left unconnected at the output will get optimized away
  defparam the_read_command_FIFO.FIFO_DEPTH = FIFO_DEPTH;
  defparam the_read_command_FIFO.FIFO_DEPTH_LOG2 = FIFO_DEPTH_LOG2;
  defparam the_read_command_FIFO.LATENCY = 2;


  // Descriptor FIFO allows for each byte lane to be written to and the data is not committed to the FIFO until the 'push' signal is asserted.
  // This differs from scfifo which commits the data any time the write signal is asserted.
  fifo_with_byteenables the_write_command_FIFO (
    .clk (clk),
    .areset (reset),
    .sreset (sw_reset),
    .write_data (writedata),
    .write_byteenables (byteenable),
    .write (write_en),
    .push (push_write_fifo),
    .read_data (write_fifo_output),
    .pop (pop_write_fifo),
    .used (write_command_used),  // this is a 'true used' signal with the full bit accounted for
    .full (write_command_full),
    .empty (write_command_empty)
  );
  defparam the_write_command_FIFO.DATA_WIDTH = DATA_WIDTH;  // we are not actually going to use all these bits and byte lanes left unconnected at the output will get optimized away
  defparam the_write_command_FIFO.FIFO_DEPTH = FIFO_DEPTH;
  defparam the_write_command_FIFO.FIFO_DEPTH_LOG2 = FIFO_DEPTH_LOG2;
  defparam the_write_command_FIFO.LATENCY = 2; 
  /**************************************** End Module Instantiations ************************************************/


  /****************************************** Combinational Signals **************************************************/
  generate  // all unnecessary signals and drivers will be optimized away
    if (MODE == 0)  // MM-->MM
    begin
      assign waitrequest = (read_command_full == 1) | (write_command_full == 1) | (sw_reset == 1);

      // information for the CSR or response blocks to use
      assign sequence_number = {write_sequence_number_d1, read_sequence_number_d1};
      assign transfer_complete_IRQ_mask = write_transfer_complete_IRQ_mask_d1;
      assign early_termination_IRQ_mask = 1'b0;
      assign error_IRQ_mask = 8'h00; 

      // read buffer flow control
      assign push_read_fifo = go_bit;
      assign read_park_enable = (read_park == 1) & (read_command_used == 1);  // we want to keep the descriptor in the FIFO when the park bit is set
      assign read_command_valid = (stop == 0) & (sw_reset == 0) & (stop_issuing_commands == 0) &
                                  (read_command_empty == 0) & (read_command_empty_d1 == 0) & (read_command_empty_d2 == 0);  // command buffer has two cycles of latency so the empty deassertion need to delayed two cycles but asserted in zero cycles, the time between commands will be at least 2 cycles so this delay is only needed coming out of the empty condition
      assign issue_read_descriptor = (read_command_valid == 1) & (read_command_ready == 1);
      assign pop_read_fifo = (issue_read_descriptor == 1) & (read_park_enable == 0);  // don't want to pop the fifo if we are in parked mode

      // write buffer flow control
      assign push_write_fifo = go_bit;
      assign write_park_enable = (write_park == 1) & (write_command_used == 1);  // we want to keep the descriptor in the FIFO when the park bit is set
      assign write_command_valid = (stop == 0) & (sw_reset == 0) & (stop_issuing_commands == 0) &
                                   (write_command_empty == 0) & (write_command_empty_d1 == 0) & (write_command_empty_d2 == 0);  // command buffer has two cycles of latency so the empty deassertion need to delayed two cycles but asserted in zero cycles, the time between commands will be at least 2 cycles so this delay is only needed coming out of the empty condition
      assign issue_write_descriptor = (write_command_valid == 1) & (write_command_ready == 1);
      assign pop_write_fifo = (issue_write_descriptor == 1) & (write_park_enable == 0);  // don't want to pop the fifo if we are in parked mode
    end
    else if (MODE == 1)  // MM-->ST
    begin
      // information for the CSR or response blocks to use
      assign sequence_number = {16'h0000, read_sequence_number_d1};
      assign transfer_complete_IRQ_mask = read_transfer_complete_IRQ_mask_d1;
      assign early_termination_IRQ_mask = 1'b0;
      assign error_IRQ_mask = 8'h00;

      assign waitrequest = (read_command_full == 1) | (sw_reset == 1);

      // read buffer flow control
      assign push_read_fifo = go_bit;
      assign read_park_enable = (read_park == 1) & (read_command_used == 1);  // we want to keep the descriptor in the FIFO when the park bit is set
      assign read_command_valid = (stop == 0) & (sw_reset == 0) & (stop_issuing_commands == 0) &
                                  (read_command_empty == 0) & (read_command_empty_d1 == 0) & (read_command_empty_d2 == 0);  // command buffer has two cycles of latency so the empty deassertion need to delayed two cycles but asserted in zero cycles, the time between commands will be at least 2 cycles so this delay is only needed coming out of the empty condition
      assign issue_read_descriptor = (read_command_valid == 1) & (read_command_ready == 1);
      assign pop_read_fifo = (issue_read_descriptor == 1) & (read_park_enable == 0);  // don't want to pop the fifo if we are in parked mode

      // write buffer flow control
      assign push_write_fifo = 0;
      assign write_park_enable = 0;
      assign write_command_valid = 0;
      assign issue_write_descriptor = 0;
      assign pop_write_fifo = 0;
    end
    else  // ST-->MM
    begin
      // information for the CSR or response blocks to use
      assign sequence_number = {write_sequence_number_d1, 16'h0000};
      assign transfer_complete_IRQ_mask = write_transfer_complete_IRQ_mask_d1;
      assign early_termination_IRQ_mask = write_early_termination_IRQ_mask_d1; 
      assign error_IRQ_mask = write_error_IRQ_mask_d1;

      assign waitrequest = (write_command_full == 1) | (sw_reset == 1);

      // read buffer flow control
      assign push_read_fifo = 0;
      assign read_park_enable = 0;
      assign read_command_valid = 0;
      assign issue_read_descriptor = 0;
      assign pop_read_fifo = 0;

      // write buffer flow control
      assign push_write_fifo = go_bit;
      assign write_park_enable = (write_park == 1) & (write_command_used == 1);  // we want to keep the descriptor in the FIFO when the park bit is set
      assign write_command_valid = (stop == 0) & (sw_reset == 0) & (stop_issuing_commands == 0) &
                                   (write_command_empty == 0) & (write_command_empty_d1 == 0) & (write_command_empty_d2 == 0);  // command buffer has two cycles of latency so the empty deassertion need to delayed two cycles but asserted in zero cycles, the time between commands will be at least 2 cycles so this delay is only needed coming out of the empty condition
      assign issue_write_descriptor = (write_command_valid == 1) & (write_command_ready == 1);
      assign pop_write_fifo = (issue_write_descriptor == 1) & (write_park_enable == 0);  // don't want to pop the fifo if we are in parked mode
    end
  endgenerate


  generate  // go bit is in a different location depending on the width of the slave port
    if (DATA_WIDTH == 256)
    begin
      assign go_bit = (writedata[255] == 1) & (write == 1) & (byteenable[31] == 1) & (waitrequest == 0);
    end
    else
    begin
      assign go_bit = (writedata[127] == 1) & (write == 1) & (byteenable[15] == 1) & (waitrequest == 0);
    end
  endgenerate

  assign write_en = (write == 1) & (waitrequest == 0);
  /**************************************** End Combinational Signals ************************************************/

endmodule
