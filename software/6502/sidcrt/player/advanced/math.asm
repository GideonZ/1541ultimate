;-----------------------------------------------------------------------
; FILE math.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Math routines used by the advanced player.
;
; Don't add routines to the file that are not used by the
; advanced player, otherwise the player will grow in size.
;
; Use 64tass version 1.53.1515 or higher to assemble the code
;-----------------------------------------------------------------------

multiply5       ; 5x = 4 + 1
                ldx inOut+1
                lda inOut
                asl
                rol inOut+1
multiply        asl
                rol inOut+1

                clc
                adc inOut
                sta inOut
                txa
                adc inOut+1
                sta inOut+1
                rts

multiply6       ; 6x = 2 * 2 + 2
                asl inOut
                rol inOut+1

                ldx inOut+1
                lda inOut
                jmp multiply

multiply60      ; 60x = 2 * 5 * 6
                asl inOut
                rol inOut+1
                jsr multiply5
                jmp multiply6

;------------------------------

; div16
;   input:
;   - 16-bit number in dividend
;   - 8-bit in divisor
;   output:
;   - 16-bit number in dividend
;   - AC: remainder
;   - XR: 0
;   - YR: unchanged
div16           ldx #16
                lda #0
-               asl dividend
                rol dividend+1
                rol a
                cmp divisor
                bcc +
                sbc divisor
                inc dividend
+               dex
                bne -
                rts

convertNumToDecDigit
                ldx #$ff
                sec
-               inx
                sbc #100
                bcs -
                adc #100
                tay
                txa
                adc #'0' - 1
                pha
                tya

                ldx #$ff
                sec
-               inx
                sbc #10
                bcs -
                adc #10 + '0'
                tay

                txa
                adc #'0' - 1
                tax

                pla
                ; now increase by one since song number is in range 1 - 256
                cpy #'9'
                bne ++
                ldy #'0' - 1
                cpx #'9'
                bne +
                adc #0
                ldx #'0' - 1
+               inx
+               iny
                rts

bcdToBin        tax
                and #$0f
                tay
                txa
                lsr
                lsr
                lsr
                lsr
                tax
                tya
                clc
                adc bcdConvert10,x
                rts

                .section data
inOut           .byte 0, 0

divisor         .byte 0
dividend        .byte 0, 0
remainder       .byte 0

bcdConvert10    .byte 0, 10, 20, 30, 40, 50, 60, 70, 80, 90
                .send