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
  This block takes the length and forms the appropriate burst count.
  Whenever one of the short access enables are asserted this block
  will post a burst of one.  Posting a burst of one isn't necessary
  but it will make it possible to add byte enable support to the
  read master at a later date.


  Revision History:

  1.0  First version
  
  1.1  Added generate logic around the internal burst count logic to prevent zero
       replication simulation bug.  In the case of a non-bursting master the
       burst count is just hardcoded to 1.

*/


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 


module read_burst_control (
  address,
  length,
  maximum_burst_count,
  short_first_access_enable,
  short_last_access_enable,
  short_first_and_last_access_enable,

  burst_count
);

  parameter BURST_ENABLE = 1;      // set to 0 to hardwire the address and write signals straight out
  parameter BURST_COUNT_WIDTH = 3;
  parameter WORD_SIZE_LOG2 = 2;    // log2(DATA WIDTH/8)
  parameter ADDRESS_WIDTH = 32;
  parameter LENGTH_WIDTH = 32;
  parameter BURST_WRAPPING_SUPPORT = 1;  // set 1 for on, set 0 for off.  This parameter can't be enabled when hte master supports programmable burst.
  localparam BURST_OFFSET_WIDTH = (BURST_COUNT_WIDTH == 1)? 1: (BURST_COUNT_WIDTH-1);

  input [ADDRESS_WIDTH-1:0] address;
  input [LENGTH_WIDTH-1:0] length;
  input [BURST_COUNT_WIDTH-1:0] maximum_burst_count;  // will be either a hardcoded input or programmable
  input short_first_access_enable;
  input short_last_access_enable;
  input short_first_and_last_access_enable;

  output wire [BURST_COUNT_WIDTH-1:0] burst_count;


  wire [BURST_COUNT_WIDTH-1:0] posted_burst;   // when the burst statemachine is used this will be the burst count posted to the fabric
  reg [BURST_COUNT_WIDTH-1:0] internal_burst_count;  // muxes posted_burst, posted_burst_d1, and '1' since we need to be able to post bursts of '1' for short accesses
  wire burst_of_one_enable;     // asserted when partial word accesses are occuring
  wire short_burst_enable;
  wire [BURST_OFFSET_WIDTH-1:0] burst_offset;


  assign burst_offset = address[BURST_OFFSET_WIDTH+WORD_SIZE_LOG2-1:WORD_SIZE_LOG2];

  // for unaligned or partial transfers we must use a burst length of 1 so that 
  assign burst_of_one_enable = (short_first_access_enable == 1) | (short_last_access_enable == 1) | (short_first_and_last_access_enable == 1) |  // when performing partial accesses use a burst length of 1
                               ((BURST_WRAPPING_SUPPORT == 1) & (burst_offset != 0));  // when the burst boundary offset is non-zero then the master isn't in burst alignment yet as so a burst of 1 needs to be posted
  assign short_burst_enable = ((length >> WORD_SIZE_LOG2) < maximum_burst_count);

generate
  if ((BURST_ENABLE == 1) & (BURST_COUNT_WIDTH > 1))
  begin
   
    always @ (maximum_burst_count or length or short_burst_enable or burst_of_one_enable)
    begin
    case ({short_burst_enable, burst_of_one_enable})
      2'b00 : internal_burst_count = maximum_burst_count;
      2'b01 : internal_burst_count = 1;  // this is when the master starts unaligned
      2'b10 : internal_burst_count = ((length >> WORD_SIZE_LOG2) & {(BURST_COUNT_WIDTH-1){1'b1}});  // this could be followed by a burst of 1 if there are a few bytes leftover
      2'b11 : internal_burst_count = 1;  // burst of 1 needs to win, this is when the master starts with very little data to transfer
    endcase
    end
 
    assign burst_count = internal_burst_count;
  end
  else
  begin
    assign burst_count = 1;  // this will be stubbed at the top level but will be used for the address and pending reads incrementing
  end
endgenerate

endmodule
