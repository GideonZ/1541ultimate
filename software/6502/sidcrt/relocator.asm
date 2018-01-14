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

cleanupVars     lda #$00
                sta BASE_ADDRESS
                sta START_LO
                sta START_HI
                sta END_LO
                sta END_HI
                rts

; relocateCode
;   input:
;   - $aa, $ab begin address of code to be relocated for that address
;   - $ac, $ad end address of code
relocateCode    lda START_HI
                sta BASE_ADDRESS

-               ldy #$00
                jsr readAddress
                tax
                lda opcodeSizes,x
                pha
                cmp #$03          ; only opcodes with 3 bytes need to be relocated
                bne +

                ldy #$02
                jsr readAddress
                tax
                and #$f0
                cmp #$d0          ; ignore I/O addresses
                beq +
                txa
                clc
                adc BASE_ADDRESS
                jsr writeAddress

+               pla
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

relocByte       tya
                sta START_LO
                txa
                clc
                adc BASE_ADDRESS
                sta START_HI
                ldy #$00
                jsr readAddress
                clc
                adc BASE_ADDRESS
                jmp writeAddress

opcodeSizes     .byte $01, $02, $01, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03
                .byte $03, $02, $01, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03

                .byte $01, $02, $01, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03
                .byte $01, $02, $01, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03

                .byte $02, $02, $02, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03
                .byte $02, $02, $02, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03

                .byte $02, $02, $02, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03
                .byte $02, $02, $02, $02, $02, $02, $02, $02, $01, $02, $01, $02, $03, $03, $03, $03
                .byte $02, $02, $01, $02, $02, $02, $02, $02, $01, $03, $01, $03, $03, $03, $03, $03