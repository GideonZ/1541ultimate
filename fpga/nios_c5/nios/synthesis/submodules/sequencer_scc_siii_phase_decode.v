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

module sequencer_scc_siii_phase_decode
    # (parameter
    
    AVL_DATA_WIDTH          =   32,
    DLL_DELAY_CHAIN_LENGTH  =   6
        
    )
    (
    
    avl_writedata,

	dqsi_phase,	
    dqs_phase,	
    dq_phase,	
    dqse_phase
    
);

	input [AVL_DATA_WIDTH - 1:0] avl_writedata;
	
	output [2:0] dqsi_phase;	
	output [6:0] dqs_phase;	
	output [6:0] dq_phase;	
	output [5:0] dqse_phase;
	
	// phase decoding.

	reg [2:0] dqsi_phase;
	reg [6:0] dqs_phase;
	reg [6:0] dq_phase;
	reg [5:0] dqse_phase;

	// decode phases
	
	always @ (*) begin
		dqsi_phase = 0;
		dqs_phase = 0;
		dq_phase = 0;
		dqse_phase = 0;

		case (DLL_DELAY_CHAIN_LENGTH)
		6: begin
			// DQSin = 60, DQS = 180, DQ = 120, DQSE = 120
			dqsi_phase = 3'b000;
			dqs_phase  = 7'b0010100;
			dq_phase   = 7'b0001100;
			dqse_phase = 6'b001000;

			case (avl_writedata[4:0])
			5'b00000: // DQS = 180, DQ = 120, DQSE = 120
				begin
					dqs_phase  = 7'b0010100;
					dq_phase   = 7'b0001100;
					dqse_phase = 6'b001000;
				end
			5'b00001: // DQS = 240, DQ = 180, DQSE = 180
				begin
					dqs_phase  = 7'b0011100;
					dq_phase   = 7'b0010100;
					dqse_phase = 6'b001100;
				end
			5'b00010: // DQS = 300, DQ = 240, DQSE = 240
				begin
					dqs_phase  = 7'b0100110;
					dq_phase   = 7'b0011100;
					dqse_phase = 6'b000110;
				end
			5'b00011: // DQS = 360, DQ = 300, DQSE = 300
				begin
					dqs_phase  = 7'b0010010;
					dq_phase   = 7'b0001010;
					dqse_phase = 6'b001011;
				end
			5'b00100: // DQS = 420, DQ = 360, DQSE = 360
				begin
					dqs_phase  = 7'b0011010;
					dq_phase   = 7'b0010010;
					dqse_phase = 6'b001111;
				end
			5'b00101: // DQS = 480, DQ = 420, DQSE = 420
				begin
					dqs_phase  = 7'b0100001;
					dq_phase   = 7'b0011010;
					dqse_phase = 6'b000101;
				end
			5'b00110: // DQS = 540, DQ = 480
				begin
					dqs_phase  = 7'b0010101;
					dq_phase   = 7'b0001101;
				end
			5'b00111: // DQS = 600, DQ = 540
				begin
					dqs_phase  = 7'b0011101;
					dq_phase   = 7'b0010101;
				end
			5'b01000: // DQS = 660, DQ = 600
				begin
					dqs_phase  = 7'b0100111;
					dq_phase   = 7'b0011101;
				end
			5'b01001: // DQS = 720, DQ = 660
				begin
					dqs_phase  = 7'b0010011;
					dq_phase   = 7'b0001011;
				end
			5'b01010: // DQS = 780, DQ = 720
				begin
					dqs_phase  = 7'b0011011;
					dq_phase   = 7'b0010011;
				end
			default : begin end
			endcase
		end
		8: begin
			// DQSin = 90, DQS = 180, DQ = 90, DQSE = 90
			
			dqsi_phase = 3'b001;
			dqs_phase  = 7'b0010100;
			dq_phase   = 7'b0000100;
			dqse_phase = 6'b001000;

			case (avl_writedata[4:0])
			5'b00000: // DQS = 180, DQ = 90, DQSE = 90
				begin
					dqs_phase  = 7'b0010100;
					dq_phase   = 7'b0000100;
					dqse_phase = 6'b001000;
				end
			5'b00001: // DQS = 225, DQ = 135, DQSE = 135
				begin
					dqs_phase  = 7'b0011100;
					dq_phase   = 7'b0001100;
					dqse_phase = 6'b001100;
				end
			5'b00010: // DQS = 270, DQ = 180, DQSE = 180
				begin
					dqs_phase  = 7'b0100100;
					dq_phase   = 7'b0010100;
					dqse_phase = 6'b010000;
				end
			5'b00011: // DQS = 315, DQ = 225, DQSE = 225
				begin
					dqs_phase  = 7'b0101110;
					dq_phase   = 7'b0011100;
					dqse_phase = 6'b000110;
				end
			5'b00100: // DQS = 360, DQ = 270, DQSE = 270
				begin
					dqs_phase  = 7'b0010010;
					dq_phase   = 7'b0000000;
					dqse_phase = 6'b001010;
				end
			5'b00101: // DQS = 405, DQ = 315, DQSE = 315
				begin
					dqs_phase  = 7'b0011010;
					dq_phase   = 7'b0001010;
					dqse_phase = 6'b001111;
				end
			5'b00110: // DQS = 450, DQ = 360, DQSE = 360
				begin
					dqs_phase  = 7'b0100010;
					dq_phase   = 7'b0010010;
					dqse_phase = 6'b010011;
				end
			5'b00111: // DQS = 495, DQ = 405, DQSE = 405
				begin
					dqs_phase  = 7'b0101001;
					dq_phase   = 7'b0011010;
					dqse_phase = 6'b000101;
				end
			5'b01000: // DQS = 540, DQ = 450
				begin
					dqs_phase  = 7'b0010101;
					dq_phase   = 7'b0000110;
				end
			5'b01001: // DQS = 585, DQ = 495
				begin
					dqs_phase  = 7'b0011101;
					dq_phase   = 7'b0001101;
				end
			5'b01010: // DQS = 630, DQ = 540
				begin
					dqs_phase  = 7'b0100101;
					dq_phase   = 7'b0010101;
				end
			5'b01011: // DQS = 675, DQ = 585
				begin
					dqs_phase  = 7'b0101111;
					dq_phase   = 7'b0011101;
				end
			5'b01100: // DQS = 720, DQ = 630
				begin
					dqs_phase  = 7'b0010011;
					dq_phase   = 7'b0000001;
				end
			5'b01101: // DQS = 765, DQ = 675
				begin
					dqs_phase  = 7'b0011011;
					dq_phase   = 7'b0001011;
				end
			5'b01110: // DQS = 810, DQ = 720
				begin
					dqs_phase  = 7'b0100011;
					dq_phase   = 7'b0010011;
				end
			default : begin end
			endcase
		end
		10: begin
			// DQSin = 72, DQS = 180, DQ = 108, DQSE = 108
			dqsi_phase = 3'b001;
			dqs_phase  = 7'b0010100;
			dq_phase   = 7'b0000100;
			dqse_phase = 6'b001100;

			case (avl_writedata[4:0])
			5'b00000: // DQS = 180, DQ = 108, DQSE = 108
				begin
					dqs_phase  = 7'b0010100;
					dq_phase   = 7'b0000100;
					dqse_phase = 6'b001100;
				end
			5'b00001: // DQS = 216, DQ = 144, DQSE = 144
				begin
					dqs_phase  = 7'b0011100;
					dq_phase   = 7'b0001100;
					dqse_phase = 6'b010000;
				end
			5'b00010: // DQS = 252, DQ = 180, DQSE = 180
				begin
					dqs_phase  = 7'b0100100;
					dq_phase   = 7'b0010100;
					dqse_phase = 6'b010100;
				end
			5'b00011: // DQS = 288, DQ = 216, DQSE = 216
				begin
					dqs_phase  = 7'b0101110;
					dq_phase   = 7'b0011100;
					dqse_phase = 6'b000110;
				end
			5'b00100: // DQS = 324, DQ = 252, DQSE = 252
				begin
					dqs_phase  = 7'b0110110;
					dq_phase   = 7'b0100100;
					dqse_phase = 6'b001010;
				end
			5'b00101: // DQS = 360, DQ = 288, DQSE = 288
				begin
					dqs_phase  = 7'b0010010;
					dq_phase   = 7'b0000010;
					dqse_phase = 6'b001111;
				end
			5'b00110: // DQS = 396, DQ = 324, DQSE = 324
				begin
					dqs_phase  = 7'b0011010;
					dq_phase   = 7'b0001010;
					dqse_phase = 6'b010011;
				end
			5'b00111: // DQS = 432, DQ = 360, DQSE = 360
				begin
					dqs_phase  = 7'b0100010;
					dq_phase   = 7'b0010010;
					dqse_phase = 6'b010111;
				end
			5'b01000: // DQS = 468, DQ = 396, DQSE = 396
				begin
					dqs_phase  = 7'b0101001;
					dq_phase   = 7'b0011010;
					dqse_phase = 6'b000101;
				end
			5'b01001: // DQS = 504, DQ = 432, DQSE = 432
				begin
					dqs_phase  = 7'b0110001;
					dq_phase   = 7'b0100010;
					dqse_phase = 6'b001001;
				end
			5'b01010: // DQS = 540, DQ = 468
				begin
					dqs_phase  = 7'b0010101;
					dq_phase   = 7'b0000101;
				end
			5'b01011: // DQS = 576, DQ = 504
				begin
					dqs_phase  = 7'b0011101;
					dq_phase   = 7'b0001101;
				end
			5'b01100: // DQS = 612, DQ = 540
				begin
					dqs_phase  = 7'b0100101;
					dq_phase   = 7'b0010101;
				end
			5'b01101: // DQS = 648, DQ = 576
				begin
					dqs_phase  = 7'b0101111;
					dq_phase   = 7'b0011101;
				end
			5'b01110: // DQS = 684, DQ = 612
				begin
					dqs_phase  = 7'b0110111;
					dq_phase   = 7'b0100101;
				end
			5'b01111: // DQS = 720, DQ = 648
				begin
					dqs_phase  = 7'b0010011;
					dq_phase   = 7'b0000011;
				end
			5'b10000: // DQS = 756, DQ = 684
				begin
					dqs_phase  = 7'b0011011;
					dq_phase   = 7'b0001011;
				end
			5'b10001: // DQS = 792, DQ = 720
				begin
					dqs_phase  = 7'b0100011;
					dq_phase   = 7'b0010011;
				end
			default : begin end
			endcase
		end
		12: begin
			// DQSin = 60, DQS = 180, DQ = 120, DQSE = 90
			dqsi_phase = 3'b001;
			dqs_phase  = 7'b0010100;
			dq_phase   = 7'b0000100;
			dqse_phase = 6'b001100;

			case (avl_writedata[4:0])
			5'b00000: // DQS = 180, DQ = 120, DQSE = 90
				begin
					dqs_phase  = 7'b0010100;
					dq_phase   = 7'b0000100;
					dqse_phase = 6'b001100;
				end
			5'b00001: // DQS = 210, DQ = 150, DQSE = 120
				begin
					dqs_phase  = 7'b0011100;
					dq_phase   = 7'b0001100;
					dqse_phase = 6'b010000;
				end
			5'b00010: // DQS = 240, DQ = 180, DQSE = 150
				begin
					dqs_phase  = 7'b0100100;
					dq_phase   = 7'b0010100;
					dqse_phase = 6'b010100;
				end
			5'b00011: // DQS = 270, DQ = 210, DQSE = 180
				begin
					dqs_phase  = 7'b0101100;
					dq_phase   = 7'b0011100;
					dqse_phase = 6'b011000;
				end
			5'b00100: // DQS = 300, DQ = 240, DQSE = 210
				begin
					dqs_phase  = 7'b0110110;
					dq_phase   = 7'b0100100;
					dqse_phase = 6'b000110;
				end
			5'b00101: // DQS = 330, DQ = 270, DQSE = 240
				begin
					dqs_phase  = 7'b0111110;
					dq_phase   = 7'b0101100;
					dqse_phase = 6'b001010;
				end
			5'b00110: // DQS = 360, DQ = 300, DQSE = 270
				begin
					dqs_phase  = 7'b0010010;
					dq_phase   = 7'b0000010;
					dqse_phase = 6'b001110;
				end
			5'b00111: // DQS = 390, DQ = 330, DQSE = 300
				begin
					dqs_phase  = 7'b0011010;
					dq_phase   = 7'b0001010;
					dqse_phase = 6'b010011;
				end
			5'b01000: // DQS = 420, DQ = 360, DQSE = 330
				begin
					dqs_phase  = 7'b0100010;
					dq_phase   = 7'b0010010;
					dqse_phase = 6'b010111;
				end
			5'b01001: // DQS = 450, DQ = 390, DQSE = 360
				begin
					dqs_phase  = 7'b0101010;
					dq_phase   = 7'b0011010;
					dqse_phase = 6'b011011;
				end
			5'b01010: // DQS = 480, DQ = 420, DQSE = 390
				begin
					dqs_phase  = 7'b0110001;
					dq_phase   = 7'b0100010;
					dqse_phase = 6'b000101;
				end
			5'b01011: // DQS = 510, DQ = 450, DQSE = 420
				begin
					dqs_phase  = 7'b0111001;
					dq_phase   = 7'b0101010;
					dqse_phase = 6'b001001;
				end
			5'b01100: // DQS = 540, DQ = 480
				begin
					dqs_phase  = 7'b0010101;
					dq_phase   = 7'b0000101;
				end
			5'b01101: // DQS = 570, DQ = 510
				begin
					dqs_phase  = 7'b0011101;
					dq_phase   = 7'b0001101;
				end
			5'b01110: // DQS = 600, DQ = 540
				begin
					dqs_phase  = 7'b0100101;
					dq_phase   = 7'b0010101;
				end
			5'b01111: // DQS = 630, DQ = 570
				begin
					dqs_phase  = 7'b0101101;
					dq_phase   = 7'b0011101;
				end
			5'b10000: // DQS = 660, DQ = 600
				begin
					dqs_phase  = 7'b0110111;
					dq_phase   = 7'b0100101;
				end
			5'b10001: // DQS = 690, DQ = 630
				begin
					dqs_phase  = 7'b0111111;
					dq_phase   = 7'b0101101;
				end
			5'b10010: // DQS = 720, DQ = 660
				begin
					dqs_phase  = 7'b0010011;
					dq_phase   = 7'b0000011;
				end
			5'b10011: // DQS = 750, DQ = 690
				begin
					dqs_phase  = 7'b0011011;
					dq_phase   = 7'b0001011;
				end
			5'b10100: // DQS = 780, DQ = 720
				begin
					dqs_phase  = 7'b0100011;
					dq_phase   = 7'b0010011;
				end
			5'b10101: // DQS = 810, DQ = 750
				begin
					dqs_phase  = 7'b0101011;
					dq_phase   = 7'b0011011;
				end
			default : begin end
			endcase
		end
		default : begin end
		endcase
	end

	
endmodule
