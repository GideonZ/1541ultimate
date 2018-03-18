;-----------------------------------------------------------------------
; FILE relocator.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Relocates code to specified address.
;-----------------------------------------------------------------------

; ZERO PAGE ADDRESSES
START_LO = $aa
START_HI = $ab
END_LO = $ac
END_HI = $ad
BASE_ADDRESS = $ae

ZERO_PAGE_ADDRESSES_RELOC = [
    START_LO,
    START_HI,
    END_LO,
    END_HI,
    BASE_ADDRESS
]

; relocateCode
;   input:
;   - $aa, $ab begin address of code to be relocated for that address
;   - $ac, $ad end address of code
relocateCode    lda START_HI
                sta BASE_ADDRESS

-               ldy #$00
                jsr readAddress
                cmp #$20            ; JSR is the only exception in opcode size between opcodes from $00-$1f, $20-$3f, $40-$5f and $60-$7f.
                bne +
                lda #$4c            ; interpret JSR as JMP

+               and #$ff - $60      ; map $e0-$ff, $c0-$df, $a0-$bf to $80-$9f and map $60-$7f, $40-$5f, $20-$3f to $00-$1f
                bpl +
                eor #$80 + $20      ; map $80-$9f to $20-$3f
+               tax
                lda opcodeSizes,x
                pha
                cmp #$03            ; only opcodes with 3 bytes need to be relocated
                bne readNext

                ldy #$02
                jsr readAddress
                tax
                and #$f0
                cmp #$d0            ; ignore I/O addresses
                beq readNext
                txa
                clc
                adc BASE_ADDRESS
                jsr writeAddress

readNext        pla
                clc
                adc START_LO
                sta START_LO
                bcc +
                inc START_HI

+               lda START_LO
                cmp END_LO
                bne -
                lda START_HI
                cmp END_HI
                bne -
                rts

relocByte       clc
                adc BASE_ADDRESS
                sta START_HI
                sty START_LO
                ldy #$00
                jsr readAddress
                clc
                adc BASE_ADDRESS
                jmp writeAddress

                ; these opcode sizes are compacted
                ; opcodes $60-$7f, $40-$5f, $20-$3f are mapped to $00-$1f, except for JSR ($20) which should be handled separately
                ; opcodes $e0-$ff, $c0-$df, $a0-$bf, $80-$9f are mapped to $20-$3f
opcodeSizes     .byte $01, $02, $01, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03

                .byte $02, $02, $02, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03
