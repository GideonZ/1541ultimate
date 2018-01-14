;-----------------------------------------------------------------------
; FILE clock.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Routines for displaying a clock. The counting of the clock should be
;   synced with the VIC chip and is adjusted based on the system's clock
;   frequency.
;
; Use 64tass version 1.53.1515 or higher to assemble the code
;-----------------------------------------------------------------------

countClock      dec frameCounter
                bpl ++
                lda framesPerSec
                sta frameCounter
                dec adjustCounter
                bpl +

                lda clockAdjust
                sta adjustCounter

adjustDir       lda #$00
                beq speedupCounter
                inc frameCounter        ; delay counter to have the time displayed accurate
                lda #$01
                sta delayFrame
                bne endAdjust
speedupCounter
                dec frameCounter        ; speedup counter to have the time displayed accurate
                lda #$01
                sta speedUpFrame
endAdjust
+               ldx #$03
-               lda time,x
                inc time,x
                cmp limit,x
                bne +
                lda #'0'
                sta time,x
                dex
                bpl -
+               rts

displayClock    ldx #$03
                ldy #$04
-               lda time,x
digit           sta $0798,y
                dey
                cpy #$02
                bne +
                lda #':'
                bne digit
+               dex
                bpl -
                rts

initClock       sei
-               lda $d012
-               cmp $d012
                beq -
                bmi --
                and #$03
                eor #$03
                bne +
                ; check for pal / drean pal-n
                tax
-               inx
                ldy #$2c
                cpy $d012
                bne -
                inx
                bmi +
                lda #$03
+               sta palntsc

; 00 = PAL (312 raster lines, 63 cycles per line)
; 01 = NTSC (263 raster lines, 65 cycles per line)
; 02 = NTSC (262 raster lines, 64 cycles per line, old VIC with bug)
; 03 = PAL Drean (312 raster lines, 65 cycles per line)

                ldy #$01        ; INC instruction to compensate timer

                tax
                lda clockAdjustValues,x
                sta clockAdjust

                txa
                beq PAL
                cmp #$03
                beq PAL
                ; NTSC
                cmp #$01
                bne +
                ldy #$00        ; DEC instruction for new NTSC model to compensate timer

+               lda #60 - 1
                bne +
PAL             lda #50 - 1
+
                sta framesPerSec
                sta frameCounter
                sty adjustDir+1
                rts

resetClock      lda #$00
                sta speedUpFrame
                sta delayFrame

                ldx #$03
                lda #'0'
-               sta time,x
                dex
                bpl -

                lda framesPerSec
                sta frameCounter
                rts

                .section data
clockAdjustValues
                .byte 7   ;PAL      312*63 / (0985248,611 - 312*63*50) - 1
                .byte 4   ;NTSC NEW 263*65 / (1022727,143 - 263*65*60) - 1
                .byte 0   ;NTSC OLD 262*64 / (1022727,143 - 262*64*60) - 1
                .byte 1   ;DREAN    312*65 / (1023444,571 - 312*65*50) - 1

clockAdjust     .word 7
adjustCounter   .word 0

framesPerSec    .byte 50 - 1
frameCounter    .byte 0

time            .byte '0', '0', '0', '0'
limit           .byte '9', '9', '5', '9'
                .send