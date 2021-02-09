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

  This logic recieves a potentially unsupported byte enable combination and
  breaks it down into supported byte enable combinations to the fabric.
  
  For example if a 64-bit write master wants to write
  to addresses 0x1 and beyond, this maps to address 0x0 with byte enables
  "11111110" asserted.  This does not contain a power of two of neighbooring
  asserted bits.  Instead this block will convert this into three writes
  all of which are supported:  "00000010", "00001100", and "11110000".  When
  this block breaks a transfer down it asserts stall so that the rest of the
  master logic will keep the outputs constant.
  
  Revision History:
  
  1.0  Initial version - Used a word distance to calculate which lanes to enable.
  
  1.1  Re-encoded version - Uses byte enables directly to calculate which lanes
                            to enable.  This allows byte enables in the middle
                            of a word to be supported as well such as '0110'.

  1.2  Bug fix to include the waitrequest for state transitions when the byte
       enable width is greater than 2.

  1.3  Added support for 64 and 128-bit byte enables (for 512/1024 bit data paths)

*/



// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 



module byte_enable_generator (
  clk,
  reset,

  // master side
  write_in,
  byteenable_in,
  waitrequest_out,

  // fabric side
  byteenable_out,
  waitrequest_in
);

  parameter BYTEENABLE_WIDTH = 4;   // valid byteenable widths are 1, 2, 4, 8, 16, 32, 64, and 128

  input clk;
  input reset;

  input write_in;                              // will enable state machine logic
  input [BYTEENABLE_WIDTH-1:0] byteenable_in;  // byteenables from master which contain unsupported groupings of byte lanes to be converted
  output wire waitrequest_out;                 // used to stall the master when fabric asserts waitrequest or access needs to be broken down

  output wire [BYTEENABLE_WIDTH-1:0] byteenable_out;   // supported byte enables to the fabric
  input waitrequest_in;                                // waitrequest from the fabric


generate
  if (BYTEENABLE_WIDTH == 1)  // for completeness...
  begin
      assign byteenable_out = byteenable_in;
      assign waitrequest_out = waitrequest_in;
  end
  else if (BYTEENABLE_WIDTH == 2)
  begin
      sixteen_bit_byteenable_FSM the_sixteen_bit_byteenable_FSM (   // pass through for the most part like the 1 bit case, has it's own module since the 4 bit module uses it
        .write_in (write_in),
        .byteenable_in (byteenable_in),
        .waitrequest_out (waitrequest_out),
        .byteenable_out (byteenable_out),
        .waitrequest_in (waitrequest_in)
      );  
  end
  else if (BYTEENABLE_WIDTH == 4)
  begin
      thirty_two_bit_byteenable_FSM the_thirty_two_bit_byteenable_FSM(
        .clk (clk),
        .reset (reset),
        .write_in (write_in),
        .byteenable_in (byteenable_in),
        .waitrequest_out (waitrequest_out),
        .byteenable_out (byteenable_out),
        .waitrequest_in (waitrequest_in)
      );  
  end
  else if (BYTEENABLE_WIDTH == 8)
  begin
      sixty_four_bit_byteenable_FSM the_sixty_four_bit_byteenable_FSM(
        .clk (clk),
        .reset (reset),
        .write_in (write_in),
        .byteenable_in (byteenable_in),
        .waitrequest_out (waitrequest_out),
        .byteenable_out (byteenable_out),
        .waitrequest_in (waitrequest_in)
      );  
  end
  else if (BYTEENABLE_WIDTH == 16)
  begin
      one_hundred_twenty_eight_bit_byteenable_FSM the_one_hundred_twenty_eight_bit_byteenable_FSM(
        .clk (clk),
        .reset (reset),
        .write_in (write_in),
        .byteenable_in (byteenable_in),
        .waitrequest_out (waitrequest_out),
        .byteenable_out (byteenable_out),
        .waitrequest_in (waitrequest_in)
      );  
  end
  else if (BYTEENABLE_WIDTH == 32)
  begin
      two_hundred_fifty_six_bit_byteenable_FSM the_two_hundred_fifty_six_bit_byteenable_FSM(
        .clk (clk),
        .reset (reset),
        .write_in (write_in),
        .byteenable_in (byteenable_in),
        .waitrequest_out (waitrequest_out),
        .byteenable_out (byteenable_out),
        .waitrequest_in (waitrequest_in)
      );
  end
  else if (BYTEENABLE_WIDTH == 64)
  begin
      five_hundred_twelve_bit_byteenable_FSM the_five_hundred_twelve_bit_byteenable_FSM (
        .clk (clk),
        .reset (reset),
        .write_in (write_in),
        .byteenable_in (byteenable_in),
        .waitrequest_out (waitrequest_out),
        .byteenable_out (byteenable_out),
        .waitrequest_in (waitrequest_in)
      );
  end
  else if (BYTEENABLE_WIDTH == 128)
  begin
      one_thousand_twenty_four_byteenable_FSM the_one_thousand_twenty_four_byteenable_FSM (
        .clk (clk),
        .reset (reset),
        .write_in (write_in),
        .byteenable_in (byteenable_in),
        .waitrequest_out (waitrequest_out),
        .byteenable_out (byteenable_out),
        .waitrequest_in (waitrequest_in)
      );
  end
endgenerate

endmodule





module one_thousand_twenty_four_byteenable_FSM (
  clk,
  reset,

  write_in,
  byteenable_in,
  waitrequest_out,

  byteenable_out,
  waitrequest_in
);

  input clk;
  input reset;

  input write_in;
  input [127:0] byteenable_in;
  output wire waitrequest_out;

  output wire [127:0] byteenable_out; 
  input waitrequest_in;


  // internal statemachine signals
  wire partial_lower_half_transfer;
  wire full_lower_half_transfer;
  wire partial_upper_half_transfer;
  wire full_upper_half_transfer;
  wire full_word_transfer;
  reg state_bit;
  wire transfer_done;
  wire advance_to_next_state;
  wire lower_enable;
  wire upper_enable;
  wire lower_stall;
  wire upper_stall;
  wire two_stage_transfer;


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      state_bit <= 0;
    end
    else
    begin
      if (transfer_done == 1)
      begin
        state_bit <= 0;
      end
      else if (advance_to_next_state == 1)
      begin
        state_bit <= 1;
      end
    end
  end

  assign partial_lower_half_transfer = (byteenable_in[63:0] != 0);
  assign full_lower_half_transfer = (byteenable_in[63:0] == 64'hFFFFFFFFFFFFFFFF);
  assign partial_upper_half_transfer = (byteenable_in[127:64] != 0);
  assign full_upper_half_transfer = (byteenable_in[127:64] == 64'hFFFFFFFFFFFFFFFF);
  assign full_word_transfer = (full_lower_half_transfer == 1) & (full_upper_half_transfer == 1);
  assign two_stage_transfer = (full_word_transfer == 0) & (partial_lower_half_transfer == 1) & (partial_upper_half_transfer == 1);

  assign advance_to_next_state = (two_stage_transfer == 1) & (lower_stall == 0) & (write_in == 1) & (state_bit == 0) & (waitrequest_in == 0);  // partial lower half transfer completed and there are bytes in the upper half that need to go out still

  assign transfer_done = ((full_word_transfer == 1) & (waitrequest_in == 0) & (write_in == 1)) | // full word transfer complete
                         ((two_stage_transfer == 0) & (lower_stall == 0) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0)) | // partial upper or lower half transfer complete
                         ((two_stage_transfer == 1) & (state_bit == 1) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0));  // partial upper and lower half transfers complete

  assign lower_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_lower_half_transfer == 1)) | // only a partial lower half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_lower_half_transfer == 1) & (state_bit == 0));  // partial lower half transfer (to be followed by an upper half transfer)

  assign upper_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_upper_half_transfer == 1)) | // only a partial upper half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_upper_half_transfer == 1) & (state_bit == 1));  // partial upper half transfer (after the lower half transfer)

  five_hundred_twelve_bit_byteenable_FSM lower_five_hundred_twelve_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (lower_enable),
    .byteenable_in (byteenable_in[63:0]),
    .waitrequest_out (lower_stall),    
    .byteenable_out (byteenable_out[63:0]),
    .waitrequest_in (waitrequest_in)
  );

  five_hundred_twelve_bit_byteenable_FSM upper_five_hundred_twelve_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (upper_enable),
    .byteenable_in (byteenable_in[127:64]),
    .waitrequest_out (upper_stall),    
    .byteenable_out (byteenable_out[127:64]),
    .waitrequest_in (waitrequest_in)
  );

  assign waitrequest_out = (waitrequest_in == 1) | ((transfer_done == 0) & (write_in == 1));

endmodule






module five_hundred_twelve_bit_byteenable_FSM (
  clk,
  reset,

  write_in,
  byteenable_in,
  waitrequest_out,

  byteenable_out,
  waitrequest_in
);


  input clk;
  input reset;

  input write_in;
  input [63:0] byteenable_in;
  output wire waitrequest_out;

  output wire [63:0] byteenable_out; 
  input waitrequest_in;


  // internal statemachine signals
  wire partial_lower_half_transfer;
  wire full_lower_half_transfer;
  wire partial_upper_half_transfer;
  wire full_upper_half_transfer;
  wire full_word_transfer;
  reg state_bit;
  wire transfer_done;
  wire advance_to_next_state;
  wire lower_enable;
  wire upper_enable;
  wire lower_stall;
  wire upper_stall;
  wire two_stage_transfer;


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      state_bit <= 0;
    end
    else
    begin
      if (transfer_done == 1)
      begin
        state_bit <= 0;
      end
      else if (advance_to_next_state == 1)
      begin
        state_bit <= 1;
      end
    end
  end

  assign partial_lower_half_transfer = (byteenable_in[31:0] != 0);
  assign full_lower_half_transfer = (byteenable_in[31:0] == 32'hFFFFFFFF);
  assign partial_upper_half_transfer = (byteenable_in[63:32] != 0);
  assign full_upper_half_transfer = (byteenable_in[63:32] == 32'hFFFFFFFF);
  assign full_word_transfer = (full_lower_half_transfer == 1) & (full_upper_half_transfer == 1);
  assign two_stage_transfer = (full_word_transfer == 0) & (partial_lower_half_transfer == 1) & (partial_upper_half_transfer == 1);

  assign advance_to_next_state = (two_stage_transfer == 1) & (lower_stall == 0) & (write_in == 1) & (state_bit == 0) & (waitrequest_in == 0);  // partial lower half transfer completed and there are bytes in the upper half that need to go out still

  assign transfer_done = ((full_word_transfer == 1) & (waitrequest_in == 0) & (write_in == 1)) | // full word transfer complete
                         ((two_stage_transfer == 0) & (lower_stall == 0) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0)) | // partial upper or lower half transfer complete
                         ((two_stage_transfer == 1) & (state_bit == 1) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0));  // partial upper and lower half transfers complete

  assign lower_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_lower_half_transfer == 1)) | // only a partial lower half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_lower_half_transfer == 1) & (state_bit == 0));  // partial lower half transfer (to be followed by an upper half transfer)

  assign upper_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_upper_half_transfer == 1)) | // only a partial upper half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_upper_half_transfer == 1) & (state_bit == 1));  // partial upper half transfer (after the lower half transfer)

  two_hundred_fifty_six_bit_byteenable_FSM lower_two_hundred_fifty_six_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (lower_enable),
    .byteenable_in (byteenable_in[31:0]),
    .waitrequest_out (lower_stall),    
    .byteenable_out (byteenable_out[31:0]),
    .waitrequest_in (waitrequest_in)
  );

  two_hundred_fifty_six_bit_byteenable_FSM upper_two_hundred_fifty_six_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (upper_enable),
    .byteenable_in (byteenable_in[63:32]),
    .waitrequest_out (upper_stall),    
    .byteenable_out (byteenable_out[63:32]),
    .waitrequest_in (waitrequest_in)
  );

  assign waitrequest_out = (waitrequest_in == 1) | ((transfer_done == 0) & (write_in == 1));

endmodule





module two_hundred_fifty_six_bit_byteenable_FSM (
  clk,
  reset,

  write_in,
  byteenable_in,
  waitrequest_out,

  byteenable_out,
  waitrequest_in
);


  input clk;
  input reset;

  input write_in;
  input [31:0] byteenable_in;
  output wire waitrequest_out;

  output wire [31:0] byteenable_out; 
  input waitrequest_in;


  // internal statemachine signals
  wire partial_lower_half_transfer;
  wire full_lower_half_transfer;
  wire partial_upper_half_transfer;
  wire full_upper_half_transfer;
  wire full_word_transfer;
  reg state_bit;
  wire transfer_done;
  wire advance_to_next_state;
  wire lower_enable;
  wire upper_enable;
  wire lower_stall;
  wire upper_stall;
  wire two_stage_transfer;


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      state_bit <= 0;
    end
    else
    begin
      if (transfer_done == 1)
      begin
        state_bit <= 0;
      end
      else if (advance_to_next_state == 1)
      begin
        state_bit <= 1;
      end
    end
  end

  assign partial_lower_half_transfer = (byteenable_in[15:0] != 0);
  assign full_lower_half_transfer = (byteenable_in[15:0] == 16'hFFFF);
  assign partial_upper_half_transfer = (byteenable_in[31:16] != 0);
  assign full_upper_half_transfer = (byteenable_in[31:16] == 16'hFFFF);
  assign full_word_transfer = (full_lower_half_transfer == 1) & (full_upper_half_transfer == 1);
  assign two_stage_transfer = (full_word_transfer == 0) & (partial_lower_half_transfer == 1) & (partial_upper_half_transfer == 1);

  assign advance_to_next_state = (two_stage_transfer == 1) & (lower_stall == 0) & (write_in == 1) & (state_bit == 0) & (waitrequest_in == 0);  // partial lower half transfer completed and there are bytes in the upper half that need to go out still

  assign transfer_done = ((full_word_transfer == 1) & (waitrequest_in == 0) & (write_in == 1)) | // full word transfer complete
                         ((two_stage_transfer == 0) & (lower_stall == 0) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0)) | // partial upper or lower half transfer complete
                         ((two_stage_transfer == 1) & (state_bit == 1) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0));  // partial upper and lower half transfers complete

  assign lower_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_lower_half_transfer == 1)) | // only a partial lower half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_lower_half_transfer == 1) & (state_bit == 0));  // partial lower half transfer (to be followed by an upper half transfer)

  assign upper_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_upper_half_transfer == 1)) | // only a partial upper half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_upper_half_transfer == 1) & (state_bit == 1));  // partial upper half transfer (after the lower half transfer)

  one_hundred_twenty_eight_bit_byteenable_FSM lower_one_hundred_twenty_eight_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (lower_enable),
    .byteenable_in (byteenable_in[15:0]),
    .waitrequest_out (lower_stall),    
    .byteenable_out (byteenable_out[15:0]),
    .waitrequest_in (waitrequest_in)
  );

  one_hundred_twenty_eight_bit_byteenable_FSM upper_one_hundred_twenty_eight_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (upper_enable),
    .byteenable_in (byteenable_in[31:16]),
    .waitrequest_out (upper_stall),    
    .byteenable_out (byteenable_out[31:16]),
    .waitrequest_in (waitrequest_in)
  );

  assign waitrequest_out = (waitrequest_in == 1) | ((transfer_done == 0) & (write_in == 1));

endmodule




module one_hundred_twenty_eight_bit_byteenable_FSM (
  clk,
  reset,

  write_in,
  byteenable_in,
  waitrequest_out,

  byteenable_out,
  waitrequest_in
);


  input clk;
  input reset;

  input write_in;
  input [15:0] byteenable_in;
  output wire waitrequest_out;

  output wire [15:0] byteenable_out; 
  input waitrequest_in;

  // internal statemachine signals
  wire partial_lower_half_transfer;
  wire full_lower_half_transfer;
  wire partial_upper_half_transfer;
  wire full_upper_half_transfer;
  wire full_word_transfer;
  reg state_bit;
  wire transfer_done;
  wire advance_to_next_state;
  wire lower_enable;
  wire upper_enable;
  wire lower_stall;
  wire upper_stall;
  wire two_stage_transfer;


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      state_bit <= 0;
    end
    else
    begin
      if (transfer_done == 1)
      begin
        state_bit <= 0;
      end
      else if (advance_to_next_state == 1)
      begin
        state_bit <= 1;
      end
    end
  end

  assign partial_lower_half_transfer = (byteenable_in[7:0] != 0);
  assign full_lower_half_transfer = (byteenable_in[7:0] == 8'hFF);
  assign partial_upper_half_transfer = (byteenable_in[15:8] != 0);
  assign full_upper_half_transfer = (byteenable_in[15:8] == 8'hFF);
  assign full_word_transfer = (full_lower_half_transfer == 1) & (full_upper_half_transfer == 1);
  assign two_stage_transfer = (full_word_transfer == 0) & (partial_lower_half_transfer == 1) & (partial_upper_half_transfer == 1);

  assign advance_to_next_state = (two_stage_transfer == 1) & (lower_stall == 0) & (write_in == 1) & (state_bit == 0) & (waitrequest_in == 0);  // partial lower half transfer completed and there are bytes in the upper half that need to go out still

  assign transfer_done = ((full_word_transfer == 1) & (waitrequest_in == 0) & (write_in == 1)) | // full word transfer complete
                         ((two_stage_transfer == 0) & (lower_stall == 0) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0)) | // partial upper or lower half transfer complete
                         ((two_stage_transfer == 1) & (state_bit == 1) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0));  // partial upper and lower half transfers complete

  assign lower_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_lower_half_transfer == 1)) | // only a partial lower half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_lower_half_transfer == 1) & (state_bit == 0));  // partial lower half transfer (to be followed by an upper half transfer)

  assign upper_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_upper_half_transfer == 1)) | // only a partial upper half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_upper_half_transfer == 1) & (state_bit == 1));  // partial upper half transfer (after the lower half transfer)

  sixty_four_bit_byteenable_FSM lower_sixty_four_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (lower_enable),
    .byteenable_in (byteenable_in[7:0]),
    .waitrequest_out (lower_stall),    
    .byteenable_out (byteenable_out[7:0]),
    .waitrequest_in (waitrequest_in)
  );

  sixty_four_bit_byteenable_FSM upper_sixty_four_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (upper_enable),
    .byteenable_in (byteenable_in[15:8]),
    .waitrequest_out (upper_stall),    
    .byteenable_out (byteenable_out[15:8]),
    .waitrequest_in (waitrequest_in)
  );

  assign waitrequest_out = (waitrequest_in == 1) | ((transfer_done == 0) & (write_in == 1));

endmodule




module sixty_four_bit_byteenable_FSM (
  clk,
  reset,

  write_in,
  byteenable_in,
  waitrequest_out,

  byteenable_out,
  waitrequest_in
);


  input clk;
  input reset;
  
  input write_in;
  input [7:0] byteenable_in;
  output wire waitrequest_out;

  output wire [7:0] byteenable_out; 
  input waitrequest_in;

  // internal statemachine signals
  wire partial_lower_half_transfer;
  wire full_lower_half_transfer;
  wire partial_upper_half_transfer;
  wire full_upper_half_transfer;
  wire full_word_transfer;
  reg state_bit;
  wire transfer_done;
  wire advance_to_next_state;
  wire lower_enable;
  wire upper_enable;
  wire lower_stall;
  wire upper_stall;
  wire two_stage_transfer;


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      state_bit <= 0;
    end
    else
    begin
      if (transfer_done == 1)
      begin
        state_bit <= 0;
      end
      else if (advance_to_next_state == 1)
      begin
        state_bit <= 1;
      end
    end
  end

  assign partial_lower_half_transfer = (byteenable_in[3:0] != 0);
  assign full_lower_half_transfer = (byteenable_in[3:0] == 4'hF);
  assign partial_upper_half_transfer = (byteenable_in[7:4] != 0);
  assign full_upper_half_transfer = (byteenable_in[7:4] == 4'hF);
  assign full_word_transfer = (full_lower_half_transfer == 1) & (full_upper_half_transfer == 1);
  assign two_stage_transfer = (full_word_transfer == 0) & (partial_lower_half_transfer == 1) & (partial_upper_half_transfer == 1);

  assign advance_to_next_state = (two_stage_transfer == 1) & (lower_stall == 0) & (write_in == 1) & (state_bit == 0) & (waitrequest_in == 0);  // partial lower half transfer completed and there are bytes in the upper half that need to go out still

  assign transfer_done = ((full_word_transfer == 1) & (waitrequest_in == 0) & (write_in == 1)) | // full word transfer complete
                         ((two_stage_transfer == 0) & (lower_stall == 0) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0)) | // partial upper or lower half transfer complete
                         ((two_stage_transfer == 1) & (state_bit == 1) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0));  // partial upper and lower half transfers complete

  assign lower_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_lower_half_transfer == 1)) | // only a partial lower half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_lower_half_transfer == 1) & (state_bit == 0));  // partial lower half transfer (to be followed by an upper half transfer)

  assign upper_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_upper_half_transfer == 1)) | // only a partial upper half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_upper_half_transfer == 1) & (state_bit == 1));  // partial upper half transfer (after the lower half transfer)

  thirty_two_bit_byteenable_FSM lower_thirty_two_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (lower_enable),
    .byteenable_in (byteenable_in[3:0]),
    .waitrequest_out (lower_stall),    
    .byteenable_out (byteenable_out[3:0]),
    .waitrequest_in (waitrequest_in)
  );

  thirty_two_bit_byteenable_FSM upper_thirty_two_bit_byteenable_FSM (
    .clk (clk),
    .reset (reset),
    .write_in (upper_enable),
    .byteenable_in (byteenable_in[7:4]),
    .waitrequest_out (upper_stall),    
    .byteenable_out (byteenable_out[7:4]),
    .waitrequest_in (waitrequest_in)
  );

  assign waitrequest_out = (waitrequest_in == 1) | ((transfer_done == 0) & (write_in == 1));

endmodule



module thirty_two_bit_byteenable_FSM (
  clk,
  reset,

  write_in,
  byteenable_in,
  waitrequest_out,

  byteenable_out,
  waitrequest_in
);


  input clk;
  input reset;

  input write_in;
  input [3:0] byteenable_in;
  output wire waitrequest_out;

  output wire [3:0] byteenable_out; 
  input waitrequest_in;


  // internal statemachine signals
  wire partial_lower_half_transfer;
  wire full_lower_half_transfer;
  wire partial_upper_half_transfer;
  wire full_upper_half_transfer;
  wire full_word_transfer;
  reg state_bit;
  wire transfer_done;
  wire advance_to_next_state;
  wire lower_enable;
  wire upper_enable;
  wire lower_stall;
  wire upper_stall;
  wire two_stage_transfer;


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      state_bit <= 0;
    end
    else
    begin
      if (transfer_done == 1)
      begin
        state_bit <= 0;
      end
      else if (advance_to_next_state == 1)
      begin
        state_bit <= 1;
      end
    end
  end

  assign partial_lower_half_transfer = (byteenable_in[1:0] != 0);
  assign full_lower_half_transfer = (byteenable_in[1:0] == 2'h3);
  assign partial_upper_half_transfer = (byteenable_in[3:2] != 0);
  assign full_upper_half_transfer = (byteenable_in[3:2] == 2'h3);
  assign full_word_transfer = (full_lower_half_transfer == 1) & (full_upper_half_transfer == 1);
  assign two_stage_transfer = (full_word_transfer == 0) & (partial_lower_half_transfer == 1) & (partial_upper_half_transfer == 1);

  assign advance_to_next_state = (two_stage_transfer == 1) & (lower_stall == 0) & (write_in == 1) & (state_bit == 0) & (waitrequest_in == 0);  // partial lower half transfer completed and there are bytes in the upper half that need to go out still

  assign transfer_done = ((full_word_transfer == 1) & (waitrequest_in == 0) & (write_in == 1)) | // full word transfer complete
                         ((two_stage_transfer == 0) & (lower_stall == 0) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0)) | // partial upper or lower half transfer complete
                         ((two_stage_transfer == 1) & (state_bit == 1) & (upper_stall == 0) & (write_in == 1) & (waitrequest_in == 0));  // partial upper and lower half transfers complete

  assign lower_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_lower_half_transfer == 1)) | // only a partial lower half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_lower_half_transfer == 1) & (state_bit == 0));  // partial lower half transfer (to be followed by an upper half transfer)

  assign upper_enable = ((write_in == 1) & (full_word_transfer == 1)) |  // full word transfer
                        ((write_in == 1) & (two_stage_transfer == 0) & (partial_upper_half_transfer == 1)) | // only a partial upper half transfer
                        ((write_in == 1) & (two_stage_transfer == 1) & (partial_upper_half_transfer == 1) & (state_bit == 1));  // partial upper half transfer (after the lower half transfer)

  sixteen_bit_byteenable_FSM lower_sixteen_bit_byteenable_FSM (
    .write_in (lower_enable),
    .byteenable_in (byteenable_in[1:0]),
    .waitrequest_out (lower_stall),    
    .byteenable_out (byteenable_out[1:0]),
    .waitrequest_in (waitrequest_in)
  );

  sixteen_bit_byteenable_FSM upper_sixteen_bit_byteenable_FSM (
    .write_in (upper_enable),
    .byteenable_in (byteenable_in[3:2]),
    .waitrequest_out (upper_stall),    
    .byteenable_out (byteenable_out[3:2]),
    .waitrequest_in (waitrequest_in)
  );

  assign waitrequest_out = (waitrequest_in == 1) | ((transfer_done == 0) & (write_in == 1));

endmodule




/**************************************************************************************************
  Fundament byte enable state machine for which 32, 64, 128, 256, 512, and 1024 bit byte enable
  statemachines will use to operate on groups of two byte enables.
***************************************************************************************************/
module sixteen_bit_byteenable_FSM (
  write_in,
  byteenable_in,
  waitrequest_out,

  byteenable_out,
  waitrequest_in
);

 
  input write_in;
  input [1:0] byteenable_in;
  output wire waitrequest_out;

  output wire [1:0] byteenable_out; 
  input waitrequest_in;

  assign byteenable_out = byteenable_in & {2{write_in}};          // all 2 bit byte enable pairs are supported, masked with write in to turn the byte lanes off when writing is disabled
  assign waitrequest_out = (write_in == 1) & (waitrequest_in == 1);  // transfer always completes on the first cycle unless waitrequest is asserted

endmodule
