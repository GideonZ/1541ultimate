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

module sequencer_scc_acv_phase_decode
	# (parameter
    
	AVL_DATA_WIDTH          =   32,
	DLL_DELAY_CHAIN_LENGTH  =   8,
	USE_2X_DLL              =   "false"
        
	)
	(
    
	avl_writedata,
	dqse_phase
    
);

	input [AVL_DATA_WIDTH - 1:0] avl_writedata;

	// Arria V and Cyclone V only have dqse_phase control

	
	// phase decoding.
	output [3:0] dqse_phase;
	reg [3:0] dqse_phase;

generate
if (USE_2X_DLL == "true")
begin
	always @ (*) begin : decode_2x

		dqse_phase = 4'b0111;

		case (avl_writedata[2:0])
		3'b000: 
			begin
				dqse_phase = 4'b0100;
			end
		3'b001: 
			begin
				dqse_phase = 4'b0101;
			end
		3'b010: 
			begin
				dqse_phase = 4'b0110;
			end
		3'b011: 
			begin
				dqse_phase = 4'b0111;
			end
		3'b100: 
			begin
				dqse_phase = 4'b1000;
			end
		3'b101: 
			begin
				dqse_phase = 4'b1001;
			end
		3'b110: 
			begin
				dqse_phase = 4'b1010;
			end
		3'b111: 
			begin
				dqse_phase = 4'b1011;
			end
		default : begin end
		endcase
	end

end
else
begin
	always @ (*) begin : decode

		// DQSE = 270
		dqse_phase = 4'b0110;

		case (avl_writedata[2:0])
		3'b000: // DQSE = 90
			begin
				dqse_phase = 4'b0010;
			end
		3'b001: // DQSE = 135
			begin
				dqse_phase = 4'b0011;
			end
		3'b010: // DQSE = 180
			begin
				dqse_phase = 4'b0100;
			end
		3'b011: // DQSE = 225
			begin
				dqse_phase = 4'b0101;
			end
		3'b100: // DQSE = 270
			begin
				dqse_phase = 4'b0110;
			end
		3'b101: // DQSE = 315
			begin
				dqse_phase = 4'b1111;
			end
		3'b110: // DQSE = 360
			begin
				dqse_phase = 4'b1000;
			end
		3'b111: // DQSE = 405
			begin
				dqse_phase = 4'b1001;
			end
		default : begin end
		endcase
	end
end
endgenerate

endmodule
