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


// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 

module altera_mem_if_sequencer_rst
(
   clk,
   rst,
   clken_out,
   reset_out
);
   timeunit 1ns;
   timeprecision 1ps;

   parameter DEPTH = 2;
   parameter CLKEN_LAGS_RESET = 0;

   localparam EARLY_RST_TAP = (CLKEN_LAGS_RESET != 0) ? 0 : 1;

   input                               clk;
   input                               rst;
   output                              clken_out;
   output                              reset_out;

   (*preserve*) reg  [2: 0]            altera_reset_synchronizer_int_chain;

   wire                                w_sync_rst_input;
   reg   [(DEPTH-1): 0]                r_sync_rst_chain;

   reg                                 r_sync_rst_dly;
   reg                                 r_sync_rst;
   reg                                 r_early_rst;

    assign w_sync_rst_input = altera_reset_synchronizer_int_chain[2];



   assign clken_out                    =~r_early_rst;
   assign reset_out                    = r_sync_rst;



initial
begin
   altera_reset_synchronizer_int_chain <= '1;
end

always @(posedge clk)
begin
   altera_reset_synchronizer_int_chain[2:0]
      <= {altera_reset_synchronizer_int_chain[1:0], ~rst};
end


initial
begin
   r_sync_rst_chain <= {DEPTH{1'b1}};
end

always @(posedge clk)
begin
   if (w_sync_rst_input == 1'b1)
   begin
      r_sync_rst_chain <= {DEPTH{1'b1}};
   end
   else
   begin
      r_sync_rst_chain <= {1'b0, r_sync_rst_chain[DEPTH-1:1]};
   end
end

initial
begin
   r_sync_rst_dly <= 1'b1;
   r_sync_rst     <= 1'b1;
   r_early_rst    <= 1'b1;
end
always @(posedge clk)
begin
   r_sync_rst_dly <= r_sync_rst_chain[DEPTH-1];

   case ({r_sync_rst_dly, r_sync_rst_chain[1], r_sync_rst})
      3'b000:   r_sync_rst <= 1'b0; 
      3'b001:   r_sync_rst <= 1'b0;
      3'b010:   r_sync_rst <= 1'b0;
      3'b011:   r_sync_rst <= 1'b1;
      3'b100:   r_sync_rst <= 1'b1; 
      3'b101:   r_sync_rst <= 1'b1;
      3'b110:   r_sync_rst <= 1'b1;
      3'b111:   r_sync_rst <= 1'b1; 
      default:  r_sync_rst <= 1'b1;
   endcase

   case ({r_sync_rst_chain[DEPTH-1], r_sync_rst_chain[EARLY_RST_TAP]})
      2'b00:   r_early_rst <= 1'b0; 
      2'b01:   r_early_rst <= 1'b1; 
      2'b10:   r_early_rst <= 1'b0; 
      2'b11:   r_early_rst <= 1'b1; 
      default: r_early_rst <= 1'b1;
   endcase
end

endmodule

