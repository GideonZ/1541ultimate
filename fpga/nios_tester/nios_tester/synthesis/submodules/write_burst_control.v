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
  This block is responsible determine the appropriate burst count based on the
  master length register as well as the buffer watermark and eop/early termination
  conditions.

  Within this block is a burst counter which is used to control when the next burst
  is started.  This down counter is loaded with whatever burst count is presented
  to the fabric and counts down when waitrequest is deasserted.  When it reaches 1
  it can either start another burst or reach 0.  When the counter reaches 0 this is
  considered the idle state which can occur if there is not enough data buffered to
  start another burst.

  During write bursts the address and burst count must be held for all the beats.
  This block will register the address and burst count to keep these signals
  held constant to the fabric.  This block will not begin a burst until enough
  data has been buffered to start the burst so it will assert the stall signal
  to keep the write master from advancing to the next word (just like waitrequest)
  and will filter the write signal accordingly. 


  Revision History:

  1.0 Initial version
  
  1.1 Added sw_stop and stopped so that the write master will not be
      stopped in the middle of a burst write transaction.
  
  1.2 Added the sink ready and valid signals to this block and qualified the
      eop signal with them.

*/


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 


module write_burst_control (
  clk,
  reset,
  sw_reset,
  sw_stop,

  length,
  eop_enabled,
  eop,
  ready,
  valid,
  early_termination,
  address_in,
  write_in,
  max_burst_count,
  write_fifo_used,
  waitrequest,
  short_first_access_enable,
  short_last_access_enable,
  short_first_and_last_access_enable,
  last_access,  // JCJB:  new signal to let the burst counter reload logic know not to reload if the final word is being written to memory and data if buffered in FIFO

  address_out,
  write_out,
  burst_count,
  stall,
  reset_taken,
  stopped
);

  parameter BURST_ENABLE = 1;  // set to 0 to hardwire the address and write signals straight out
  parameter BURST_COUNT_WIDTH = 3;
  parameter WORD_SIZE = 4;
  parameter WORD_SIZE_LOG2 = 2;
  parameter ADDRESS_WIDTH = 32;
  parameter LENGTH_WIDTH = 32;
  parameter WRITE_FIFO_USED_WIDTH = 5;
  parameter BURST_WRAPPING_SUPPORT = 1;  // set 1 for on, set 0 for off.  This parameter can't be enabled when the master supports programmable bursting.
  localparam BURST_OFFSET_WIDTH = (BURST_COUNT_WIDTH == 1)? 1: (BURST_COUNT_WIDTH-1);

  input clk;
  input reset;
  input sw_reset;
  input sw_stop;

  input [LENGTH_WIDTH-1:0] length;
  input eop_enabled;
  input eop;
  input ready;
  input valid;
  input early_termination;
  input [ADDRESS_WIDTH-1:0] address_in;
  input write_in;
  input [BURST_COUNT_WIDTH-1:0] max_burst_count;  // will be either a hardcoded input or programmable
  input [WRITE_FIFO_USED_WIDTH:0] write_fifo_used;  // using the fifo full MSB as well
  input waitrequest;  // this needs to be the waitrequest from the fabric and not the byte enable generator since partial transfers count as burst beats
  input short_first_access_enable;
  input short_last_access_enable;
  input short_first_and_last_access_enable;
  // JCJB:  Added done input so that we can suppress the burst counter from incorrectly reloading at the end of a transfer
  input last_access;

  output wire [ADDRESS_WIDTH-1:0] address_out;
  output wire write_out;
  output wire [BURST_COUNT_WIDTH-1:0] burst_count;
  output wire stall;  // need to issue a stall if there isn't enough data buffered to start a burst
  output wire reset_taken;  // if a reset occurs in the middle of a burst larger than 1 then the write master needs to know that the burst hasn't completed yet
  output wire stopped;  // if a stop occurs in the middle of a burst larger than 1 then the write master needs to know that the burst hasn't completed yet


  reg [ADDRESS_WIDTH-1:0] address_d1;
  reg [BURST_COUNT_WIDTH-1:0] burst_counter;  // interal statemachine register
  wire idle_state;
  wire decrement_burst_counter;
  wire ready_during_idle_state;  // when there is enough data buffered to start up the burst counter state machine again
  wire ready_for_quick_burst;    // when there is enough data bufferred to start another burst immediately 
  wire burst_begin_from_idle_state;
  wire burst_begin_quickly;      // start another burst immediately after the previous burst completes
  wire burst_begin;
  wire burst_of_one_enable;      // asserted when partial word accesses are occuring or the last early termination word is being written out
  wire [BURST_COUNT_WIDTH-1:0] short_length_burst;
  wire [BURST_COUNT_WIDTH-1:0] short_packet_burst;
  wire short_length_burst_enable;
  wire short_early_termination_burst_enable;
  wire short_packet_burst_enable;
  wire [3:0] mux_select;
  reg [BURST_COUNT_WIDTH-1:0] internal_burst_count;
  reg [BURST_COUNT_WIDTH-1:0] internal_burst_count_d1;
  reg packet_complete;
  wire [BURST_OFFSET_WIDTH-1:0] burst_offset;



  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      packet_complete <= 0;
    end
    else
    begin
      if ((packet_complete == 1) & (write_fifo_used == 0))
      begin
        packet_complete <= 0;
      end
      else if ((eop == 1) & (ready == 1) & (valid == 1))
      begin
        packet_complete <= 1;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      address_d1 <= 0;
    end
    else if (burst_begin == 1)
    begin
      address_d1 <= (burst_begin_quickly == 1)? (address_in + WORD_SIZE) : address_in;
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      burst_counter <= 0;
    end
    else
    if ((burst_begin == 1) & (sw_reset == 0) & (sw_stop == 0))  // for reset and stop we need to let the burst complete so the fabric doesn't lock up
    begin
      burst_counter <= internal_burst_count;
    end
    else if (decrement_burst_counter == 1)
    begin
      burst_counter <= burst_counter - 1'b1;
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      internal_burst_count_d1 <= 0;
    end
    else if (burst_begin == 1)
    begin
      internal_burst_count_d1 <= internal_burst_count;
    end
  end


  // state machine status and control
  assign idle_state = (burst_counter == 0);  // any time idle_state is set then there is no burst underway
  assign decrement_burst_counter = (idle_state == 0) & (waitrequest == 0);

  // control for all the various cases that a burst of one beat needs to be posted
  assign burst_offset = address_in[BURST_OFFSET_WIDTH+WORD_SIZE_LOG2-1:WORD_SIZE_LOG2];
  assign burst_of_one_enable = (short_first_access_enable == 1) | (short_last_access_enable == 1) | (short_first_and_last_access_enable == 1) | (early_termination == 1) |
                               ((BURST_WRAPPING_SUPPORT == 1) & (idle_state == 1) & (burst_offset != 0)) |  // need to make sure bursts start on burst boundaries
                               ((BURST_WRAPPING_SUPPORT == 1) & (idle_state == 0) & (burst_offset != (max_burst_count - 1)));  // need to make sure bursts start on burst boundaries
  assign short_length_burst_enable = ((length >> WORD_SIZE_LOG2) < max_burst_count) & (eop_enabled == 0) & (burst_of_one_enable == 0);
  assign short_early_termination_burst_enable = (short_packet_burst_enable == 0) & ((length >> WORD_SIZE_LOG2) < max_burst_count) & (eop_enabled == 1) & (burst_of_one_enable == 0);  // trim back the burst count regardless if there is enough data buffered for a full burst
  assign short_packet_burst_enable = (eop_enabled == 1) & (packet_complete == 1) & (write_fifo_used < max_burst_count) & (burst_of_one_enable == 0);

  // various burst amounts that are not the max burst count or 1 that feed the internal_burst_count mux.  short_length_burst is used when short_length_burst_enable or short_early_termination_burst_enable is asserted.
generate
if (BURST_COUNT_WIDTH > 1) begin
  assign short_length_burst = (length >> WORD_SIZE_LOG2) & {(BURST_COUNT_WIDTH-1){1'b1}};
  assign short_packet_burst = (write_fifo_used & {(BURST_COUNT_WIDTH-1){1'b1}});
end
else begin
  assign short_length_burst = 1'b1;
  assign short_packet_burst = 1'b1;
end
endgenerate

  // since the write master may not have enough data buffered in the FIFO to start a burst the FIFO fill level must be checked before starting another burst
  assign ready_during_idle_state = (burst_of_one_enable == 1) |  // burst of one is only enabled when there is data in the write fifo so write_fifo_used doesn't need to be checked in this case
                                   ((write_fifo_used >= short_length_burst) & (short_length_burst_enable == 1)) |
                                   ((write_fifo_used >= short_length_burst) & (short_early_termination_burst_enable == 1)) |
                                   ((write_fifo_used >= short_packet_burst) & (short_packet_burst_enable == 1)) |
                                    (write_fifo_used >= max_burst_count);

  // same as ready_during_idle_state only we need to make sure there is more data in the fifo than the burst being posted (since the FIFO is in the middle of being popped)
  assign ready_for_quick_burst = (length >= (max_burst_count << WORD_SIZE_LOG2)) & (burst_of_one_enable == 0) &  // address and length lags by one clock cycle so this will let the state machine catch up
                                 (  ((write_fifo_used > short_length_burst) & (short_length_burst_enable == 1)) |
                                    ((write_fifo_used > short_length_burst) & (short_early_termination_burst_enable == 1)) |
                                    ((write_fifo_used > short_packet_burst) & (short_packet_burst_enable == 1)) |
                                     (write_fifo_used > max_burst_count)  );


  // burst begin signals used to start up the burst counter state machine
  assign burst_begin_from_idle_state = (write_in == 1) & (idle_state == 1) & (ready_during_idle_state == 1);   // start the state machine up again
  /* JCJB:  added qualifier (last_access == 0) to make sure when the last beat of the last burst completes we don't reload the burst
            counter if there is ample data already buffered for the next descriptor.  The burst counter being non-zero is what
            drives write_out high.  This qualifier is not needed for burst_begin_from_idle_state since there is already an idle cycle between
            writes so that write_in will already be low in time and buffered data won't reload the burst counter.
  */
  assign burst_begin_quickly = (last_access == 0) & (write_in == 1) & (burst_counter == 1) & (waitrequest == 0) & (ready_for_quick_burst == 1); // enough data is buffered to start another burst immediately after the current burst
  assign burst_begin = (burst_begin_quickly == 1) | (burst_begin_from_idle_state == 1);

  assign mux_select = {short_packet_burst_enable, short_early_termination_burst_enable, short_length_burst_enable, burst_of_one_enable};

  // one-hot mux that selects the appropriate burst count to present to the fabric
  always @ (short_length_burst or short_packet_burst or max_burst_count or mux_select)
  begin
    case (mux_select)
      4'b0001 : internal_burst_count = 1;
      4'b0010 : internal_burst_count = short_length_burst;
      4'b0100 : internal_burst_count = short_length_burst;
      4'b1000 : internal_burst_count = short_packet_burst;
      default : internal_burst_count = max_burst_count;
    endcase
  end


generate
  if (BURST_ENABLE == 1)
  begin
    // outputs that need to be held constant throughout the entire burst transaction
    assign address_out = address_d1;
    assign burst_count = internal_burst_count_d1;
    assign write_out = (idle_state == 0);
    assign stall = (idle_state == 1);
    assign reset_taken = (sw_reset == 1) & (idle_state == 1);  // for bursts of 1 the write master logic will handle the correct reset timing
	assign stopped = (sw_stop == 1) & (idle_state == 1);       // for bursts of 1 the write master logic will handle the correct stop timing
  end
  else
  begin
    assign address_out = address_in;
    assign burst_count = 1;  // this will be stubbed at the top level
    assign write_out = write_in;
    assign stall = 0;
    assign reset_taken = sw_reset;
	assign stopped = sw_stop;
  end
endgenerate

endmodule
