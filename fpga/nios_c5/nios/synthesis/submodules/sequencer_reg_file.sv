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


`timescale 1 ps / 1 ps

// ******
// reg_file
// ******
//
// Register file
//
// General Description
// -------------------
//
// This component stores all configuration/parameterization information
// that other components need to calibrate.
//
// Architecture
// ------------
//
// The PHY Manager is organized as an 
//    - Avalon Interface: it's a Memory-Mapped interface to the Avalon
//      Bus.
//    - Register File: The "register file" of read/write registers
//

module sequencer_reg_file (
	// Avalon Interface
	
	avl_clk,
	avl_reset_n,
	avl_address,
	avl_write,
	avl_writedata,
	avl_read,
	avl_readdata,
	avl_waitrequest,
	avl_be
);

parameter AVL_DATA_WIDTH = 32;
parameter AVL_ADDR_WIDTH = 4;
parameter AVL_NUM_SYMBOLS = 4;
parameter AVL_SYMBOL_WIDTH = 8;
parameter REGISTER_RDATA   = 0;
parameter NUM_REGFILE_WORDS = 16;
parameter DEBUG_REG_FILE_WORD = 2;

input avl_clk;
input avl_reset_n;
input [AVL_ADDR_WIDTH - 1:0] avl_address;
input avl_write;
input [AVL_DATA_WIDTH - 1:0] avl_writedata;
input [AVL_NUM_SYMBOLS - 1:0] avl_be;
input avl_read;
output [AVL_DATA_WIDTH - 1:0] avl_readdata;
output avl_waitrequest;

// synthesis translate_off
//Internal versions of request signals
reg [AVL_ADDR_WIDTH-1 : 0] int_addr;
reg [AVL_NUM_SYMBOLS - 1 : 0] int_be;
reg [AVL_DATA_WIDTH - 1 : 0] int_rdata;
reg [AVL_DATA_WIDTH - 1 : 0] int_rdata_reg;
// synthesis translate_on
logic int_waitrequest;
// synthesis translate_off
reg [AVL_DATA_WIDTH - 1 : 0] int_wdata;
logic [AVL_DATA_WIDTH - 1 : 0] int_wdata_wire;

reg [AVL_DATA_WIDTH-1 : 0] reg_file [0 : NUM_REGFILE_WORDS-1] /* synthesis syn_ramstyle = "logic" */;

integer i, b;

// synthesis translate_on
//State machine states
typedef enum int unsigned {
	INIT,
	IDLE,
	WRITE2,
	READ2,
	READ3,
	READ4
} avalon_state_t;

avalon_state_t state;

always_ff @ (posedge avl_clk or negedge avl_reset_n) begin
	if (~avl_reset_n)
		state <= INIT;
	else begin
		if (state == READ2)
			state <= READ3;
		else if ((state == READ3) && (REGISTER_RDATA)) 
			state <= READ4;
		else if (state == IDLE) 
			if (avl_read)
				state <= READ2;
			else if (avl_write)
				state <= WRITE2;
			else
				state <= IDLE;
		else 
			state <= IDLE;
	end
end

assign int_waitrequest = (state == IDLE) || (state == WRITE2) || ((state == READ4) && (REGISTER_RDATA)) || ((state == READ3) && (REGISTER_RDATA == 0)) ? 1'b0 : 1'b1;

// synthesis translate_off
generate
if (0) begin
// synthesis translate_on

altsyncram	altsyncram_component (
				.aclr0 (!avl_reset_n),
				.address_a (avl_address),
				.address_b (avl_address),
				.byteena_a (avl_be),
				.clock0 (avl_clk),
				.clocken0 (1'b1),
				.data_a (avl_writedata),
				.q_b (avl_readdata),
				.wren_a (avl_write));
defparam
	altsyncram_component.address_aclr_b = "CLEAR0",
	altsyncram_component.address_reg_b = "CLOCK0",
	altsyncram_component.clock_enable_input_a = "BYPASS",
	altsyncram_component.clock_enable_input_b = "BYPASS",
	altsyncram_component.clock_enable_output_b = "BYPASS",
	altsyncram_component.intended_device_family = "Stratix III",
	altsyncram_component.lpm_type = "altsyncram",
	altsyncram_component.operation_mode = "DUAL_PORT",	altsyncram_component.outdata_reg_b = REGISTER_RDATA ? "CLOCK0" : "UNREGISTERED",
	altsyncram_component.power_up_uninitialized = "FALSE",
	altsyncram_component.ram_block_type = "MLAB",
	altsyncram_component.rdcontrol_reg_b = "CLOCK0",
	altsyncram_component.numwords_a = NUM_REGFILE_WORDS,
	altsyncram_component.numwords_b = NUM_REGFILE_WORDS,
	altsyncram_component.widthad_a = AVL_ADDR_WIDTH,
	altsyncram_component.widthad_b = AVL_ADDR_WIDTH,
	altsyncram_component.width_a = AVL_DATA_WIDTH,
	altsyncram_component.width_b = AVL_DATA_WIDTH,
	altsyncram_component.width_byteena_a = AVL_NUM_SYMBOLS,
	altsyncram_component.width_byteena_b = AVL_NUM_SYMBOLS;
	
// synthesis translate_off
end
endgenerate

always_ff @ (posedge avl_clk or negedge avl_reset_n) begin
	if (~avl_reset_n) begin
		int_addr <= 0;
		int_wdata <= 0;
		int_be <= 0;
	end
	else if (int_waitrequest == 0) begin
		int_addr  <= avl_address;
		int_wdata <= avl_writedata;
		int_be    <= avl_be;
	end
end

always_ff @ (posedge avl_clk or negedge avl_reset_n) begin
	if (~avl_reset_n) begin
		int_rdata <= 0;
	end
	else begin
		if (state == READ2) 
			if (int_addr < NUM_REGFILE_WORDS) begin
				int_rdata <= reg_file[int_addr];
			end
			else begin
				int_rdata <= 0;
			end
		else
			int_rdata <= 0;
	end
end

property p_illegal_read_addr;
	@(posedge avl_clk)
	disable iff (!avl_reset_n)
	(state == READ2) |-> (int_addr < NUM_REGFILE_WORDS);
endproperty

a_illegal_read_addr : assert property (p_illegal_read_addr);

always_comb begin
	int_wdata_wire <= reg_file[int_addr];
	for (b=0; b < AVL_NUM_SYMBOLS; b++)
		if (int_be[b])
			int_wdata_wire[(b+1)*AVL_SYMBOL_WIDTH-1-:AVL_SYMBOL_WIDTH] <= int_wdata[(b+1)*AVL_SYMBOL_WIDTH-1-:AVL_SYMBOL_WIDTH];
end

always_ff @ (posedge avl_clk or negedge avl_reset_n) begin
	if (~avl_reset_n) begin
		for (i=0; i < NUM_REGFILE_WORDS; i++)
			reg_file[i] <= 0;
	end
	else begin
		i = 0;
		if (state == WRITE2) 
			if (int_addr < NUM_REGFILE_WORDS) begin
				reg_file[int_addr] <= int_wdata_wire;
			end
			else begin
			end
	end
end

property p_illegal_write_addr;
	@(posedge avl_clk)
	disable iff (!avl_reset_n)
	(state == WRITE2) |-> (int_addr < NUM_REGFILE_WORDS);
endproperty

a_illegal_write_addr : assert property (p_illegal_write_addr);

generate
	if (REGISTER_RDATA) begin
		
		always_ff @ (posedge avl_clk or negedge avl_reset_n) begin
			if (~avl_reset_n)
				int_rdata_reg <= 0;
			else
				int_rdata_reg <= int_rdata;
		end

		assign avl_readdata = int_rdata_reg;
	end
	else
		assign avl_readdata = int_rdata;
endgenerate

// synthesis translate_on

assign avl_waitrequest = ((state == IDLE) && ((avl_read == 1) || (avl_write == 1))) ? 1'b1 : int_waitrequest;

// synthesis translate_off
//The register file has a specific word which is expected to be the current
wire [15:0] current_seq_stage;
wire [15:0] current_seq_group;

assign current_seq_stage = reg_file[DEBUG_REG_FILE_WORD][15:0];
assign current_seq_group = reg_file[DEBUG_REG_FILE_WORD][31:16];

// synthesis translate_on


endmodule
