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

; detectSystem
;   input: none
;   output:
;   - AC: system clock type
;       00 = PAL (312 raster lines, 63 cycles per line)
;       01 = NTSC (263 raster lines, 65 cycles per line)
;       02 = NTSC (262 raster lines, 64 cycles per line, old VIC with bug)
;       03 = PAL Drean (312 raster lines, 65 cycles per line)
;   - YR: SID model
;       00 = 8580
;       01 = 6581
;       02 = unknown
detectSystem    jsr extraPlayer.codeStart + extraPlayer.clock.detectC64Clock
                pha
                jsr detectSidModel
                tay
                pla
                rts

detectSidModel  jsr extraPlayer.codeStart + extraPlayer.detectSidModel
                txa             ; output 0 = 8580, 1 = 6581, 2 = unknown
                rts
