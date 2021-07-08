
package pkg_6502_opcodes is

type t_opcode_array is array(0 to 255) of string(1 to 13);

constant opcode_array : t_opcode_array := (

    "BRK          ", "ORA ($nn,X)  ", "HLT*         ", "ASO*($nn,X)  ", 
    "NOP*$nn      ", "ORA $nn      ", "ASL $nn      ", "ASO*$nn      ", 
    "PHP          ", "ORA #        ", "ASL A        ", "ANC*#        ", 
    "NOP*$nnnn    ", "ORA $nnnn    ", "ASL $nnnn    ", "ASO*$nnnn    ", 

    "BPL rel      ", "ORA ($nn),Y  ", "HLT*         ", "ASO*($nn),Y  ", 
    "NOP*$nn,X    ", "ORA $nn,X    ", "ASL $nn,X    ", "ASO*$nn,X    ", 
    "CLC          ", "ORA $nnnn,Y  ", "NOP*         ", "ASO*$nnnn,Y  ", 
    "NOP*$nnnn,X  ", "ORA $nnnn,X  ", "ASL $nnnn,X  ", "ASO*$nnnn,X  ", 

    "JSR $nnnn    ", "AND ($nn,X)  ", "HLT*         ", "RLA*($nn,X)  ", 
    "BIT $nn      ", "AND $nn      ", "ROL $nn      ", "RLA*$nn      ", 
    "PLP          ", "AND #        ", "ROL A        ", "ANC*#        ", 
    "BIT $nnnn    ", "AND $nnnn    ", "ROL $nnnn    ", "RLA*$nnnn    ", 

    "BMI rel      ", "AND ($nn),Y  ", "HLT*         ", "RLA*($nn),Y  ", 
    "NOP*$nn,X    ", "AND $nn,X    ", "ROL $nn,X    ", "RLA*$nn,X    ", 
    "SEC          ", "AND $nnnn,Y  ", "NOP*         ", "RLA*$nnnn,Y  ", 
    "NOP*$nnnn,X  ", "AND $nnnn,X  ", "ROL $nnnn,X  ", "RLA*$nnnn,X  ", 

    "RTI          ", "EOR ($nn,X)  ", "HLT*         ", "LSE*($nn,X)  ", 
    "NOP*$nn      ", "EOR $nn      ", "LSR $nn      ", "LSE*$nn      ", 
    "PHA          ", "EOR #        ", "LSR A        ", "ALR*#        ", 
    "JMP $nnnn    ", "EOR $nnnn    ", "LSR $nnnn    ", "LSE*$nnnn    ", 

    "BVC rel      ", "EOR ($nn),Y  ", "HLT*         ", "LSE*($nn),Y  ", 
    "NOP*$nn,x    ", "EOR $nn,X    ", "LSR $nn,X    ", "LSE*$nn,X    ", 
    "CLI          ", "EOR $nnnn,Y  ", "NOP*         ", "LSE*$nnnn,Y  ", 
    "JMP*$nnnn  $$", "EOR $nnnn,X  ", "LSR $nnnn,X  ", "LSE*$nnnn,X  ", 

    "RTS          ", "ADC ($nn,X)  ", "HLT*         ", "RRA*($nn,X)  ", 
    "NOP*$nn      ", "ADC $nn      ", "ROR $nn      ", "RRA*$nn      ", 
    "PLA          ", "ADC #        ", "ROR A        ", "ARR*#        ", 
    "JMP ($nnnn)  ", "ADC $nnnn    ", "ROR $nnnn    ", "RRA*$nnnn    ", 

    "BVS rel      ", "ADC ($nn),Y  ", "HLT*         ", "RRA*($nn),Y  ", 
    "NOP*$nn,x    ", "ADC $nn,X    ", "ROR $nn,X    ", "RRA*$nn,X    ", 
    "SEI          ", "ADC $nnnn,Y  ", "NOP*         ", "RRA*$nnnn,Y  ", 
    "JMP*(ABS,X)$$", "ADC $nnnn,X  ", "ROR $nnnn,X  ", "RRA*$nnnn,X  ", 

    "NOP*#        ", "STA ($nn,X)  ", "NOP*#        ", "SAX*($nn,X)  ", 
    "STY $nn      ", "STA $nn      ", "STX $nn      ", "SAX*$nn      ", 
    "DEY          ", "NOP*#        ", "TXA          ", "XAA*         ", 
    "STY $nnnn    ", "STA $nnnn    ", "STX $nnnn    ", "SAX*$nnnn    ", 

    "BCC          ", "STA ($nn),Y  ", "HLT*         ", "AHX*($nn),Y  ", 
    "STY $nn,X    ", "STA $nn,X    ", "STX $nn,Y    ", "SAX*$nn,Y    ", 
    "TYA          ", "STA $nnnn,Y  ", "TXS          ", "TAS*$nnnn,Y  ", 
    "SHY*$nnnn,X  ", "STA $nnnn,X  ", "SHX*$nnnn,Y  ", "AHX*$nnnn,Y  ", 

    "LDY #        ", "LDA ($nn,X)  ", "LDX #        ", "LAX*($nn,X)  ", 
    "LDY $nn      ", "LDA $nn      ", "LDX $nn      ", "LAX*$nn      ", 
    "TAY          ", "LDA #        ", "TAX          ", "LAX*#        ", 
    "LDY $nnnn    ", "LDA $nnnn    ", "LDX $nnnn    ", "LAX*$nnnn    ", 

    "BCS          ", "LDA ($nn),Y  ", "HLT*         ", "LAX*($nn),Y  ", 
    "LDY $nn,X    ", "LDA $nn,X    ", "LDX $nn,Y    ", "LAX*$nn,Y    ", 
    "CLV          ", "LDA $nnnn,Y  ", "TSX          ", "LAS*$nnnn,Y  ", 
    "LDY $nnnn,X  ", "LDA $nnnn,X  ", "LDX $nnnn,Y  ", "LAX*$nnnn,Y  ", 

    "CPY #        ", "CMP ($nn,X)  ", "NOP*#        ", "DCM*($nn,X)  ", 
    "CPY $nn      ", "CMP $nn      ", "DEC $nn      ", "DCM*$nn      ", 
    "INY          ", "CMP #        ", "DEX          ", "AXS*# (used!)", 
    "CPY $nnnn    ", "CMP $nnnn    ", "DEC $nnnn    ", "DCM*$nnnn    ", 

    "BNE          ", "CMP ($nn),Y  ", "HLT*         ", "DCM*($nn),Y  ", 
    "NOP*$nn,X    ", "CMP $nn,X    ", "DEC $nn,X    ", "DCM*$nn,X    ", 
    "CLD          ", "CMP $nnnn,Y  ", "NOP*         ", "DCM*$nnnn,Y  ", 
    "NOP*$nnnn,X  ", "CMP $nnnn,X  ", "DEC $nnnn,X  ", "DCM*$nnnn,X  ", 

    "CPX #        ", "SBC ($nn,X)  ", "NOP*#        ", "INS*($nn,X)  ", 
    "CPX $nn      ", "SBC $nn      ", "INC $nn      ", "INS*$nn      ", 
    "INX          ", "SBC #        ", "NOP          ", "SBC*#        ", 
    "CPX $nnnn    ", "SBC $nnnn    ", "INC $nnnn    ", "INS*$nnnn    ", 

    "BEQ          ", "SBC ($nn),Y  ", "HLT*         ", "INS*($nn),Y  ", 
    "NOP*$nn,S    ", "SBC $nn,X    ", "INC $nn,X    ", "INS*$nn,X    ", 
    "SED          ", "SBC $nnnn,Y  ", "NOP*         ", "INS*$nnnn,Y  ", 
    "NOP*$nnnn,X  ", "SBC $nnnn,X  ", "INC $nnnn,X  ", "INS*$nnnn,X  " );

type t_oper_array is array(0 to 7) of string(1 to 4);

constant c_shift_oper_array : t_oper_array := (
    "ASL ", "ROL ", "LSR ", "ROR ", "NOP ", "TST ", "DEC ", "INC " );
    
constant c_alu_oper_array : t_oper_array := (
    "OR  ", "AND ", "XOR ", "ADD ", "NOP ", "TST ", "CMP ", "SUB " );
    

end;
