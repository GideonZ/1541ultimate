;-----------------------------------------------------------------------
; FILE timebar.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Routines for displaying the time bar used by the advanced player.
;
; Use 64tass version 1.53.1515 or higher to assemble the code
;-----------------------------------------------------------------------

initTimeBar     lda #$00
                sta barPosition
                sta barCounter+0
                sta barCounter+1
                sta barPosChar
                sta speedCounter

                jsr songLengthToBin
                jsr calcBarTimers

                lda frameSkipCounts+0
                sta barCounter+0
                lda frameSkipCounts+1
                sta barCounter+1

                lda #$06
                sta $d027     ; sprite 0 color

                lda #$f6
                sta $d001     ; y-pos
                lda #$00
                sta $d015
                sta $d000     ; x-pos
                sta $d010
                sta $d017
                sta $d01b     ; sprite in front of char
                sta $d01c
                sta $d01d
                rts

afterInit       ldx #12
                lda #$ff
-               dex
spriteLocation  sta $0380,x
                bne -

                inx           ; enable first sprite
                stx $d015
                rts

countTimeBar    lda barMode
                beq slowBar

                ldy speedCounter

                lda barPosition
                and #$07
                clc
                adc speedValues,y
                cmp #$08
                bcc +
                inc barPosChar
+               sta barPosition

                lda $d000
                clc
                adc speedValues,y
                sta $d000
                bcc +
                inc $d010
+
                iny
                cpy speedValLen
                bne +
                ldy #0
+               sty speedCounter
                jmp handleBarChar

slowBar         lda speedUpFrame
                beq +
                lda #0
                sta speedUpFrame
                dec barCounter+0
                lda barCounter+0
                cmp #$ff
                bne +
                dec barCounter+1
+
                lda delayFrame
                beq +
                lda #0
                sta delayFrame
                inc barCounter+0
                bne +
                inc barCounter+1
+
                dec barCounter+0
                bne noResetCount
                dec barCounter+1
                bpl noResetCount

                inc $d000
                bne +
                inc $d010
+
                inc barPosition

                lda barPosition
                and #$1f
                asl
                tay
                lda frameSkipCounts+0,y
                sta barCounter+0
                lda frameSkipCounts+1,y
                sta barCounter+1

                lda barPosition
                and #$07
                bne +
                inc barPosChar

handleBarChar   ldy barPosChar
                dey
                cpy #$28
                bcs +
                lda #$06
                sta $dbc0,y
+
noResetCount    rts

songLengthToBin lda #$00
                sta songLengthBin+0
                lda currentSong
                asl
                tax

                lda extraPlayLoc
                adc #>songLength - codeStart
                sta slDataLoc2+2
                sta slDataLoc3+2

slDataLoc2      lda songLength+0,x
                pha
slDataLoc3      lda songLength+1,x
                jsr math.bcdToBin
                sta songLengthBin+0
                pla
                jsr math.bcdToBin
                sta math.inOut+0
                lda #$00
                sta math.inOut+1
                jsr math.multiply60

                lda math.inOut
                adc songLengthBin+0
                sta math.inOut
                bcc +
                inc math.inOut+1
+
                lda math.inOut+0
                sta songLengthBin+0
                lda math.inOut+1
                sta songLengthBin+1

                lda palntsc
                beq pal
                cmp #$03
                beq pal
                jsr math.multiply6        ; multiply by 6 for NTSC
                clc
                bcc +
pal             jsr math.multiply5        ; multiply by 5 for PAL

+               lda math.inOut+0
                sta songLengthFrm+0
                lda math.inOut+1
                sta songLengthFrm+1
                rts

calcBarTimers   lda songLengthBin+1
                bne moreThan7Sec
                lda songLengthBin+0
                cmp #$07
                bcs moreThan7Sec

lessThan7Sec    ldx #1
                stx barMode

                dex
                stx speedCounter

                lda songLengthFrm+0
                sta barDivider
                sta speedValLen
                sta math.divisor

                lda #32
                sta math.dividend+0
                stx math.dividend+1
                lda songLengthFrm+0

                jsr math.div16
                sta math.remainder

                ; x is already 0 since previous math.div16 always returns x = 0
-               lda math.dividend+0
                sta speedValues,x

                lda barDivider
                sec
                sbc math.remainder
                sta barDivider
                bpl +
                inc speedValues,x
                clc
                adc speedValLen
+               sta barDivider

                inx
                cpx speedValLen
                bne -
                rts

moreThan7Sec    lda songLengthFrm+0
                sta framesLeft+0
                sta math.dividend+0

                lda songLengthFrm+1
                sta framesLeft+1
                sta math.dividend+1

                lda #32
                sta barPixelsLeft
                sta barDivider
                sta math.divisor

                jsr math.div16
                sta math.remainder

                ldy #$00
                sty barMode
-               lda framesLeft+0
                sta math.dividend+0
                lda framesLeft+1
                sta math.dividend+1
                lda barPixelsLeft
                sta math.divisor

                jsr math.div16

                lda barDivider
                sec
                sbc math.remainder
                bpl ++

                inc math.dividend+0
                bne +
                inc math.dividend+1
+
                clc
                adc #32
+               sta barDivider

                lda math.dividend+0
                sta frameSkipCounts+0,y
                lda math.dividend+1
                sta frameSkipCounts+1,y

                lda framesLeft
                sec
                sbc math.dividend+0
                sta framesLeft
                bcs +
                dec framesLeft+1
+
                lda framesLeft+1
                sec
                sbc math.dividend+1
                sta framesLeft+1

                iny
                iny
                dec barPixelsLeft
                bne -
                rts

                .section data
framesLeft      .byte 0, 0
barPixelsLeft   .byte 0

barCounter      .byte 0, 0
barPosition     .byte 0
barPosChar      .byte 0
barDivider      .byte 0

speedCounter    .byte 0
barMode         .byte 0

speedValLen     .byte 0

speedValues
frameSkipCounts
                .rept 2*32
                  .byte 0
                .next
                .send
