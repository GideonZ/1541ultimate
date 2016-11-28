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
  This block is used to communicate information back and forth between the SGDMA
  and the host.  When the response port is not set to streaming then this block
  will be used to generate interrupts for the host.  The address span of this block
  differs depending on whether the enhanced features are enabled.  The enhanced
  features enables sequence number readback capabilties.  The address map is as follows:

  Enhanced features off:

  Bytes     Access Type     Description
  -----     -----------     -----------
  0-3       R               Status(1)
  4-7       R/W             Control(2)
  8-12      R               Descriptor Watermark (write watermark[15:0],read watermark [15:0])
  13-15     R               Response Watermark
  16-31     N/A             <Reserved>


  Enhanced features on:

  Bytes     Access Type     Description
  -----     -----------     -----------
  0-3       R               Status(1)
  4-7       R/W             Control(2)
  8-12      R               Descriptor Watermark (write watermark[15:0],read watermark [15:0])
  13-15     R               Response Watermark
  16-20     R               Sequence Number (write sequence[15:0],read sequence[15:0])
  21-31     N/A             <Reserved>

  (1)  Writing to the interrupt bit of the status register clears the interrupt bit (when applicable)
  (2)  Writing to the software reset bit will clear the entire register (as well as all the registers for the entire SGDMA)

  Status Register:

  Bits      Description
  ----      -----------
  0         Busy
  1         Descriptor Buffer Empty
  2         Descriptor Buffer Full
  3         Response Buffer Empty
  4         Response Buffer Full
  5         Stop State
  6         Reset State
  7         Stopped on Error
  8         Stopped on Early Termination
  9         IRQ
  10-31     <Reserved>

  Control Register:

  Bits      Description
  ----      -----------
  0         Stop (will also be set if a stop on error/early termination condition occurs)
  1         Software Reset
  2         Stop on Error
  3         Stop on Early Termination
  4         Global Interrupt Enable Mask
  5         Stop descriptors (stops the dispatcher from issuing more read/write commands)
  6-31      <Reserved>

  
  Author:  JCJB
  Date:  08/18/2010

  1.0 - Initial release
  
  1.1 - Removed delayed reset, added set and hold sw_reset
  
  1.2 - Updated the sw_reset register to be set when control[1] is set instead of one cycle after.
        This will prevent the read or write masters from starting back up when reset while in the stop state.
  
  1.3 - Added the stop dispatcher bit (5) to the control register
*/


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 



module csr_block (
  clk,
  reset,
  
  csr_writedata,
  csr_write,
  csr_byteenable,
  csr_readdata,
  csr_read,
  csr_address,
  csr_irq,

  done_strobe,
  busy,
  descriptor_buffer_empty,
  descriptor_buffer_full,
  stop_state,
  stopped_on_error,
  stopped_on_early_termination,
  reset_stalled,
  stop,
  sw_reset,
  stop_on_error,
  stop_on_early_termination,
  stop_descriptors,
  sequence_number,
  descriptor_watermark,
  response_watermark,
  response_buffer_empty,
  response_buffer_full,
  transfer_complete_IRQ_mask,
  error_IRQ_mask,
  early_termination_IRQ_mask,
  error,
  early_termination
);

  parameter ADDRESS_WIDTH = 3;
  localparam CONTROL_REGISTER_ADDRESS = 3'b001;


  input clk;
  input reset;

  input [31:0] csr_writedata;
  input csr_write;
  input [3:0] csr_byteenable;
  output wire [31:0] csr_readdata;
  input csr_read;
  input [ADDRESS_WIDTH-1:0] csr_address;
  output wire csr_irq;

  input done_strobe;
  input busy;
  input descriptor_buffer_empty;
  input descriptor_buffer_full;
  input stop_state;      // when the DMA runs into some error condition and you have enabled the stop on error (or when the stop control bit is written to)
  input reset_stalled;   // the read or write master could be in the middle of a transfer/burst so it might take a while to flush the buffers
  output wire stop;
  output reg stopped_on_error;
  output reg stopped_on_early_termination;
  output reg sw_reset;
  output wire stop_on_error;
  output wire stop_on_early_termination;
  output wire stop_descriptors;
  input [31:0] sequence_number;
  input [31:0] descriptor_watermark;
  input [15:0] response_watermark;
  input response_buffer_empty;
  input response_buffer_full;
  input transfer_complete_IRQ_mask;
  input [7:0] error_IRQ_mask;
  input early_termination_IRQ_mask;
  input [7:0] error;
  input early_termination;

  /* Internal wires and registers */
  wire [31:0] status;
  reg [31:0] control;
  reg [31:0] readdata;
  reg [31:0] readdata_d1;
  reg irq;  // writing to the status register clears the irq bit
  wire set_irq;
  wire clear_irq;
  wire set_stopped_on_error;
  wire set_stopped_on_early_termination;
  wire set_stop;
  wire clear_stop;
  wire global_interrupt_enable;
  wire sw_reset_strobe;  // this strobe will be one cycle earlier than sw_reset
  wire set_sw_reset;
  wire clear_sw_reset;






  /********************************************** Registers ***************************************************/
  // read latency is 1 cycle
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      readdata_d1 <= 0;
    end
    else if (csr_read == 1)
    begin
      readdata_d1 <= readdata;
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      control[31:1] <= 0;
    end
    else
    begin
      if (sw_reset_strobe == 1)  // reset strobe is a strobe due to this sync reset
      begin
        control[31:1] <= 0;
      end
      else
      begin
        if ((csr_address == CONTROL_REGISTER_ADDRESS) & (csr_write == 1) & (csr_byteenable[0] == 1))
        begin
          control[7:1] <= csr_writedata[7:1];  // stop bit will be handled seperately since it can be set by the csr slave port access or the SGDMA hitting an error condition
        end
        if ((csr_address == CONTROL_REGISTER_ADDRESS) & (csr_write == 1) & (csr_byteenable[1] == 1))
        begin
          control[15:8] <= csr_writedata[15:8];
        end
        if ((csr_address == CONTROL_REGISTER_ADDRESS) & (csr_write == 1) & (csr_byteenable[2] == 1))
        begin
          control[23:16] <= csr_writedata[23:16];
        end
        if ((csr_address == CONTROL_REGISTER_ADDRESS) & (csr_write == 1) & (csr_byteenable[3] == 1))
        begin
          control[31:24] <= csr_writedata[31:24];
        end
      end
    end
  end

  
  // control bit 0 (stop) is set by different sources so handling it seperately
  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      control[0] <= 0;
    end
    else
    begin
      if (sw_reset_strobe == 1)
      begin
        control[0] <= 0;
      end
      else
      begin
        case ({set_stop, clear_stop})
          2'b00: control[0] <= control[0];
          2'b01: control[0] <= 1'b0;
          2'b10: control[0] <= 1'b1;
          2'b11: control[0] <= 1'b1;  // setting will win, this case happens control[0] is being set to 0 (resume) at the same time an error/early termination stop condition occurs 
        endcase
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      sw_reset <= 0;
    end
    else
    begin
      if (set_sw_reset == 1)
      begin
        sw_reset <= 1;
      end
      else if (clear_sw_reset == 1)
      begin
        sw_reset <= 0;
      end
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      stopped_on_error <= 0;
    end
    else
    begin
      case ({set_stopped_on_error, clear_stop})
        2'b00: stopped_on_error <= stopped_on_error;
        2'b01: stopped_on_error <= 1'b0;
        2'b10: stopped_on_error <= 1'b1;
        2'b11: stopped_on_error <= 1'b0;
      endcase
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      stopped_on_early_termination <= 0;
    end
    else
    begin
      case ({set_stopped_on_early_termination, clear_stop})
        2'b00: stopped_on_early_termination <= stopped_on_early_termination;
        2'b01: stopped_on_early_termination <= 1'b0;
        2'b10: stopped_on_early_termination <= 1'b1;
        2'b11: stopped_on_early_termination <= 1'b0;
      endcase
    end
  end


  always @ (posedge clk or posedge reset)
  begin
    if (reset)
    begin
      irq <= 0;
    end
    else
    begin
      if (sw_reset_strobe == 1)
      begin
        irq <= 0;
      end
      else
      begin
        case ({clear_irq, set_irq})
          2'b00: irq <= irq;
          2'b01: irq <= 1'b1;
          2'b10: irq <= 1'b0;
          2'b11: irq <= 1'b1;  // setting will win over a clear
        endcase
      end
    end
  end
  /******************************************** End Registers *************************************************/



  /**************************************** Combinational Signals *********************************************/
  generate
    if (ADDRESS_WIDTH == 3)
    begin  
      always @ (csr_address or status or control or descriptor_watermark or response_watermark or sequence_number)
      begin
        case (csr_address)
          3'b000: readdata = status;
          3'b001: readdata = control;
          3'b010: readdata = descriptor_watermark;
          3'b011: readdata = response_watermark;
          default: readdata = sequence_number;  // all other addresses will decode to the sequence number
        endcase  
      end
    end
    else
    begin
      always @ (csr_address or status or control or descriptor_watermark or response_watermark)
      begin
        case (csr_address)
          3'b000: readdata = status;
          3'b001: readdata = control;
          3'b010: readdata = descriptor_watermark;
          default: readdata = response_watermark;  // all other addresses will decode to the response watermark
        endcase  
      end
    end
  endgenerate


  assign clear_irq = (csr_address == 0) & (csr_write == 1) & (csr_byteenable[1] == 1) & (csr_writedata[9] == 1);  // this is the IRQ bit
  assign set_irq = (global_interrupt_enable == 1) & (done_strobe == 1) &       // transfer ended and interrupts are enabled
                   ((transfer_complete_IRQ_mask == 1) |                        // transfer ended and the transfer complete IRQ is enabled
                    ((error & error_IRQ_mask) != 0) |                          // transfer ended with an error and this IRQ is enabled
                    ((early_termination & early_termination_IRQ_mask) == 1));  // transfer ended early due to early termination and this IRQ is enabled
  assign csr_irq = irq;

  assign clear_stop = (csr_address == CONTROL_REGISTER_ADDRESS) & (csr_write == 1) & (csr_byteenable[0] == 1) & (csr_writedata[0] == 0);
  assign set_stopped_on_error = (done_strobe == 1) & (stop_on_error == 1) & (error != 0);  // when clear_stop is set then the stopped_on_error register will be cleared
  assign set_stopped_on_early_termination = (done_strobe == 1) & (stop_on_early_termination == 1) & (early_termination == 1);  // when clear_stop is set then the stopped_on_early_termination register will be cleared
  assign set_stop = ((csr_address == CONTROL_REGISTER_ADDRESS) & (csr_write == 1) & (csr_byteenable[0] == 1) & (csr_writedata[0] == 1)) |  // host set the stop bit
                    (set_stopped_on_error == 1) |  // SGDMA setup to stop when an error occurs from the write master
                    (set_stopped_on_early_termination == 1) ;  // SGDMA setup to stop when the write master overflows
  assign stop = control[0];

  assign set_sw_reset = (csr_address == CONTROL_REGISTER_ADDRESS) & (csr_write == 1) & (csr_byteenable[0] == 1) & (csr_writedata[1] == 1);
  assign clear_sw_reset = (sw_reset == 1) & (reset_stalled == 0);
  
  assign sw_reset_strobe = control[1];
  assign stop_on_error = control[2];
  assign stop_on_early_termination = control[3];
  assign global_interrupt_enable = control[4];
  assign stop_descriptors = control[5];

  assign csr_readdata = readdata_d1;
  assign status = {{22{1'b0}}, irq, stopped_on_early_termination, stopped_on_error, sw_reset, stop_state, response_buffer_full, response_buffer_empty, descriptor_buffer_full, descriptor_buffer_empty, busy};  // writing to the lower byte of the status register clears the irq bit
  /**************************************** Combinational Signals *********************************************/
  
endmodule
