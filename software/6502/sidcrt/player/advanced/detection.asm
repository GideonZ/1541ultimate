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
;   - AC: SID model
;       00 = 8580
;       01 = 6581
;       02 = unknown
detectSidModel  lda #$ff        ; make sure the check is not done on a bad line
-               cmp $d012
                bne -
                lda #$48        ; test bit should be set
                sta $d412
                sta $d40f
                lsr             ; activate sawtooth waveform
                sta $d412
                lda $d41b
                tax
                and #$fe
                bne unknownSid  ; unknown SID chip, most likely emulated or no SID in socket
                lda $d41b       ; try to read another time where the value should always be $03 on a real SID for all SID models
                cmp #$03
                beq +
unknownSid      ldx #$02
+               txa
                rts             ; output 0 = 8580, 1 = 6581, 2 = unknown
