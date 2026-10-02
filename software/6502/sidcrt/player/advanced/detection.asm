;-----------------------------------------------------------------------
; FILE detection.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Dectection routines for system info.
;-----------------------------------------------------------------------

; detectC64Clock
;   input: none
;   output:
;   - AC: system clock type
;       00 = PAL (312 raster lines, 63 cycles per line)
;       01 = NTSC (263 raster lines, 65 cycles per line)
;       02 = NTSC (262 raster lines, 64 cycles per line, old VIC with bug)
;       04 = PAL Drean (312 raster lines, 65 cycles per line)
detectC64Clock
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
                lda #$04
+               rts

; detectSidModel
;   input: none
;   output:
;   - AC: SID model of the SID at $D400
;       00 = 8580
;       01 = 6581
;       02 = unknown
detectSidModel  ldx #$00

; detectSidModelAt
;   input:
;   - XR: offset of the SID from $D400 ($00-$E0)
;   output:
;   - AC: SID model
;       00 = 8580
;       01 = 6581
;       02 = unknown
;   Indexed stores take one cycle more than absolute ones, but they write in their last cycle,
;   so the reads of $D41B still come exactly 4 and 10 cycles after the sawtooth is started.
detectSidModelAt
                lda #$ff        ; make sure the check is not done on a bad line
-               cmp $d012
                bne -
                lda #$48        ; test bit should be set
                sta $d412,x
                sta $d40f,x
                lsr             ; activate sawtooth waveform
                sta $d412,x
                lda $d41b,x
                tay
                and #$fe
                bne unknownSid  ; unknown SID chip, most likely emulated or no SID in socket
                lda $d41b,x     ; try to read another time where the value should always be $03 on a real SID for all SID models
                cmp #$03
                beq +
unknownSid      ldy #$02
+               tya
                rts             ; output 0 = 8580, 1 = 6581, 2 = unknown
