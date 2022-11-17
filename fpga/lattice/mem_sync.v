// ============================================================================
//                           COPYRIGHT NOTICE
// Copyright 2013 Lattice Semiconductor Corporation
// ALL RIGHTS RESERVED
// This confidential and proprietary software may be used only as authorized by
// a licensing agreement from Lattice Semiconductor Corporation.
// The entire notice above must be reproduced on all authorized copies and
// copies may only be made to the extent permitted by a licensing agreement 
// from Lattice Semiconductor Corporation.
//
// Lattice Semiconductor Corporation      TEL  : 1-800-Lattice (USA and Canada)
// 5555 NE Moore Court                           408-826-6000 (other locations)
// Hillsboro, OR 97124                    web  : http://www.latticesemi.com/
// U.S.A                                  email: techsupport@latticesemi.com
// =============================================================================
// Module     : mem_sync.v
// Description: 
//   - Performs start up sync needed to avoid issues on DDR memory bus
//   - Performs code update in operation without interrupting interface operation
// =============================================================================
`timescale 1ns/1ps
`define CNT_WIDTH 2

module mem_sync (
  //inputs
  start_clk,
  rst,
  dll_lock,
  pll_lock,
  update,
  //outputs
  pause,
  stop,
  freeze,
  uddcntln,
  dll_rst,
  ddr_rst,
  ready
);

// -------------------------------------------------   
//  PORT DECLARATION
// -------------------------------------------------
input       start_clk;  // oscillator clk or other constant running low speed 
                        // clk. Note that this clk should not be coming from 
                        // clk sources that this module will stop or reset 
                        // (e.g. ECLKSYNC, CLKDIV, etc.)
input       rst;        // Active high reset signal
input       dll_lock;   // Lock signal from DDRDLL
input       pll_lock;   // Lock signal from PLL
input       update;     // Signal to trigger code update

output wire pause;      // Pause signal for DQSBUF
output wire stop;       // Stop signal for ECLKSYNC
output wire freeze;     // Freeze signal for DDRDLL
output wire uddcntln;   // Update code signal for DDRDLL
output reg  dll_rst;    // Reset signal for DDRDLL
output wire ddr_rst;    // Reset signal for CLKDIV and DDR components
output wire ready;      // Ready signal to indicate sync done

// -------------------------------------------------   
//  SIGNAL DECLARATION
// ------------------------------------------------- 
reg [`CNT_WIDTH:0] count;       // Counter for both 4T and 8T delay cycles
reg [5:0]          cs_memsync /*synthesis syn_preserve = 1*/ ;
                                // FSM Current state
reg [5:0]          ns_memsync;  // FSM Next state
reg [1:0]          flag;        // Signal to determine next FSM state
reg [1:0]          flag_d;      // Registered flag_d signal
reg                lock_d1;     // Sync register for pll_lock & dll_lock input
reg                lock_d2;     // Sync register for pll_lock & dll_lock input
reg                ddr_rst_d1;  // DDR reset when rst input is detected
                   
wire               counter_4t;  // Signal for counter to count up to 4T only
wire               ddr_rst_d;   // DDR reset output of FSM

// -------------------------------------------------   
//  PARAMETERS
// ------------------------------------------------- 
// FSM States
// Each bit is equivalent to an fsm output in the following order
//     FORMAT:       freeze stop ddr_rst pause uddcntln ready
localparam INIT            = 6'b000010;
localparam FREEZE          = 6'b100010;
localparam STOP            = 6'b110010;
localparam DDR             = 6'b111010;
localparam PAUSE           = 6'b000110;
localparam UDDCNTLN        = 6'b000100;
localparam READY           = 6'b000011;
localparam UPDATE_PAUSE    = 6'b000111;
localparam UPDATE_UDDCNTLN = 6'b000101;

// Maximum Count
localparam COUNT_8T        = 7; // 8T timing
localparam COUNT_4T        = 3; // 4T timing
localparam COUNT_LOCK      = 5; // 8T timing for synchronized lock

// -------------------------------------------------   
//  CONTINUOUS ASSIGNMENTS
// ------------------------------------------------- 
assign ready     = cs_memsync[0];
assign uddcntln  = cs_memsync[1];
assign pause     = cs_memsync[2];
assign ddr_rst_d = cs_memsync[3];
assign stop      = cs_memsync[4];
assign freeze    = cs_memsync[5];
assign ddr_rst   = ddr_rst_d || ddr_rst_d1;

// Assert counter_4t to count only up to CNT_4T parameter
assign counter_4t = cs_memsync[5] || cs_memsync[2];

// -------------------------------------------------   
//  SYNCHRONIZER FOR DLL_LOCK
// ------------------------------------------------- 
always @ (posedge start_clk or posedge rst) begin
  if (rst) begin
    lock_d1 <= 1'b0;
    lock_d2 <= 1'b0;
  end
  else begin
    lock_d1 <= dll_lock & pll_lock;
    lock_d2 <= lock_d1;
  end
end

// -------------------------------------------------   
//  CURRENT STATE & OUTPUT REGISTERS
// -------------------------------------------------    
always @ (posedge start_clk or posedge rst) begin	
  if (rst) begin
    cs_memsync <= INIT; 
    dll_rst    <= 1'b1;
    ddr_rst_d1 <= 1'b1;
    flag       <= 1'b0;
  end
  else begin
    cs_memsync <= ns_memsync;
    dll_rst    <= 1'b0;
    ddr_rst_d1 <= 1'b0;
    flag       <= flag_d;
  end
end

// -------------------------------------------------     
//  RESET SEQUENCE ONE-HOT FSM
// -------------------------------------------------   
always @ (*) begin

  case(cs_memsync) 		/* synthesis full_case parallel_case */

    // INITIAL STATE
    INIT: begin     
      // Wait for DDRDLL lock
      if (lock_d2 && (flag==0) && (count == COUNT_LOCK)) begin
          ns_memsync = FREEZE;
          flag_d     = flag;
      end
      else if ( (flag!=0 ) && (count==COUNT_8T) ) begin
        if (flag[1]) begin
          ns_memsync = READY;
          flag_d     = flag;
          end
        else if (flag[0]) begin
          ns_memsync = PAUSE;
          flag_d     = 2'd0;          
        end
      end
      else begin
        ns_memsync = cs_memsync;
        flag_d     = flag;
      end
    end

    // ASSERT FREEZE STATE
    FREEZE: begin    
      if (count == COUNT_4T) begin
        if (flag[0]) begin
          ns_memsync = INIT;
          flag_d     = flag;
          end
        else begin
          ns_memsync = STOP;
          flag_d     = flag;
          end
      end
      else begin
        ns_memsync = cs_memsync;
        flag_d     = flag;
      end      
    end

    // ASSERT STOP STATE
    STOP: begin
      if (count == COUNT_4T) begin
        if (flag[0]) begin
          ns_memsync = FREEZE;
          flag_d     = flag;
        end
        else begin
          ns_memsync = DDR;
          flag_d     = flag;
        end
      end
      else begin
        ns_memsync = cs_memsync;
        flag_d     = flag;
      end 
    end

    // ASSERT DDR_RST STATE
    DDR: begin
      flag_d = 2'd1;
      if (count == COUNT_4T) begin
        ns_memsync = STOP;
      end
      else begin
        ns_memsync = cs_memsync;
      end 
    end

    // ASSERT PAUSE STATE
    PAUSE: begin
      if (count == COUNT_4T) begin
        if (flag[0]) begin
          ns_memsync = INIT;
          flag_d     = 2'd2;
        end
        else begin
          ns_memsync = UDDCNTLN;
          flag_d     = flag;
        end
      end
      else begin
        ns_memsync = cs_memsync;
        flag_d     = flag;
      end 
    end

    // ASSERT UDDCNTLN STATE
    UDDCNTLN: begin
      flag_d = 2'd1;
      if (count == COUNT_4T) begin
        ns_memsync = PAUSE;
      end
      else begin
        ns_memsync = cs_memsync;
      end 
    end
    
    // ASSERT READY STATE
    READY: begin
      flag_d = 2'd0;
      // Check if DDRDLL is still locked
      if (!lock_d2) begin
        ns_memsync = INIT;
      end
      // Check if there is an update request
      else if (update) begin
        ns_memsync = UPDATE_PAUSE;
      end
      else begin
        ns_memsync = cs_memsync;
      end
    end
    
    // ASSERT PAUSE STATE during code update
    UPDATE_PAUSE: begin
      if (count == COUNT_4T) begin
        if (flag[0]) begin
          ns_memsync = READY;
          flag_d     = flag;
        end
        else begin
          ns_memsync = UPDATE_UDDCNTLN;
          flag_d     = flag;
        end
      end
      else begin
        ns_memsync = cs_memsync;
        flag_d     = flag;
      end 
    end

    // ASSERT UDDCNTLN STATE during code update
    UPDATE_UDDCNTLN: begin
      flag_d = 2'd1;
      if (count == COUNT_4T) begin
        ns_memsync = UPDATE_PAUSE;
      end
      else begin
        ns_memsync = cs_memsync;
      end 
    end
    
    // Default State
    default: begin
      ns_memsync = cs_memsync;
      flag_d     = flag;
    end
    
  endcase
end


// ------------------------------------------------- 
//  COUNTER
// ------------------------------------------------- 

always @(posedge start_clk or posedge rst) begin
  if (rst) begin
    count <= 'h0;
  end
  else if ((counter_4t && (count==COUNT_4T)) || 
          ((cs_memsync==INIT) && !lock_d2) || 
          ((flag==0) && (count==COUNT_LOCK)) ||
          ((cs_memsync==READY)&&ready))
  begin
    count <= 'h0;
  end
  else begin
    count <= count + 1;
  end
end

endmodule //mem_sync
