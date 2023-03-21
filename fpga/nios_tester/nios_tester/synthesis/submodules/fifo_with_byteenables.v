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
  This FIFO module behaves similar to scfifo in legacy mode.  It has an
  output latency of 1 or 2 clock cycles and does not support look ahead
  mode.  Unlike scfifo this FIFO allows you to write to any of the byte
  lanes before committing the word.  This allows you to write the full
  word in multiple clock cycles and then you assert the "push" signal to
  commit the data.  To read data out of the FIFO assert "pop" and wait
  1 or 2 clock cycles for valid data to arrive.
  
  Version 1.1
  
  1.0 - Uses 'altsyncram' which will not be optimized away if the
        FIFO inputs are grounded.  This will need to be replaced
        with inferred memory once Quartus II supports inferred
        with byte enables (currently it instantiates multiple
        seperate memories.

  1.1 - Seperated the asynchronous reset into seperate async. and sync.
        resets.
*/


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 


module fifo_with_byteenables (
  clk,
  areset,
  sreset,
  write_data,
  write_byteenables,
  write,
  push,
  read_data,
  pop,
  used,
  full,
  empty
);


  parameter DATA_WIDTH = 32;
  parameter FIFO_DEPTH = 128;
  parameter FIFO_DEPTH_LOG2 = 7;  // this impacts the width of the used port so it can't be local
  parameter LATENCY = 1;  // number of clock cycles after asserting 'pop' that valid data comes out



  input clk;
  input areset;
  input sreset;
  input [DATA_WIDTH-1:0] write_data;
  input [(DATA_WIDTH/8)-1:0] write_byteenables;
  input write;
  input push;  // when you have written to all the byte lanes assert this to commit the word (you can use it at the same time as the byte enables)
  output wire [DATA_WIDTH-1:0] read_data;
  input pop;   // use this to read a word out of the FIFO
  output wire [FIFO_DEPTH_LOG2:0] used;
  output wire full;
  output wire empty;

  reg [FIFO_DEPTH_LOG2-1:0] write_address;
  reg [FIFO_DEPTH_LOG2-1:0] read_address;
  reg [FIFO_DEPTH_LOG2:0] internal_used;
  wire internal_full;
  wire internal_empty;


  always @ (posedge clk or posedge areset)
  begin
    if (areset)
    begin
      write_address <= 0;
    end
    else
    begin
      if (sreset)
      begin
        write_address <= 0;
      end
      else if (push == 1)
      begin
        write_address <= write_address + 1'b1;
      end
    end
  end

  always @ (posedge clk or posedge areset)
  begin
    if (areset)
    begin
      read_address <= 0;
    end
    else
    begin
      if (sreset)
      begin
        read_address <= 0;
      end
      else if (pop == 1)
      begin
        read_address <= read_address + 1'b1;
      end
    end
  end


  // TODO:  Change this to an inferrered RAM when Quartus II supports byte enables for inferred RAM
  altsyncram the_dp_ram (
    .clock0 (clk),
    .wren_a (write),
    .byteena_a (write_byteenables),
    .data_a (write_data),
    .address_a (write_address),
    .q_b (read_data),
    .address_b (read_address)
  );
  defparam the_dp_ram.operation_mode = "DUAL_PORT";  // simple dual port (one read, one write port)
  defparam the_dp_ram.lpm_type = "altsyncram";
  defparam the_dp_ram.read_during_write_mode_mixed_ports = "DONT_CARE";
  defparam the_dp_ram.power_up_uninitialized = "TRUE";
  defparam the_dp_ram.byte_size = 8;
  defparam the_dp_ram.width_a = DATA_WIDTH;
  defparam the_dp_ram.width_b = DATA_WIDTH;
  defparam the_dp_ram.widthad_a = FIFO_DEPTH_LOG2;
  defparam the_dp_ram.widthad_b = FIFO_DEPTH_LOG2;
  defparam the_dp_ram.width_byteena_a = (DATA_WIDTH/8);
  defparam the_dp_ram.numwords_a = FIFO_DEPTH;
  defparam the_dp_ram.numwords_b = FIFO_DEPTH;
  defparam the_dp_ram.address_reg_b = "CLOCK0";
  defparam the_dp_ram.outdata_reg_b = (LATENCY == 2)? "CLOCK0" : "UNREGISTERED";


  
  always @ (posedge clk or posedge areset)
  begin
    if (areset)
    begin
      internal_used <= 0;
    end
    else
    begin
      if (sreset)
      begin
        internal_used <= 0;
      end
      else
      begin
        case ({push, pop})
          2'b01: internal_used <= internal_used - 1'b1;
          2'b10: internal_used <= internal_used + 1'b1;
          default: internal_used <= internal_used;
        endcase
      end
    end
  end


  assign internal_empty = (read_address == write_address) & (internal_used == 0);
  assign internal_full = (write_address == read_address) & (internal_used != 0);

  assign used = internal_used;    // this signal reflects the number of words in the FIFO
  assign empty = internal_empty;  // combinational so it'll glitch a little bit
  assign full = internal_full;    // dito

endmodule
