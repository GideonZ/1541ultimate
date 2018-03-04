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
;   - XR: SIDFX detection
;       00 = no SIDFX
;       01 = SIDFX found
detectSystem    jsr extraPlayer.codeStart + extraPlayer.clock.detectC64Clock
                pha
                jsr detectSidModel
                tay
                pla
                rts

detectSidModel  jsr sidFx.detectSidFx
                beq detectNormalSid

                jsr getRequiredSidInfo

                jsr sidFx.configureSidFx
                ; AC = detected SID model (0 = 8580, 1 = 6581, 2 = unknown)
                ldx #$01        ; 1 = SIDFX found
                rts

detectNormalSid jsr extraPlayer.codeStart + extraPlayer.detectSidModel
                txa             ; 0 = 8580, 1 = 6581, 2 = unknown
                ldx #$00        ; 0 = no SIDFX
                rts

getRequiredSidInfo
                ldy #$7a
                jsr readHeader
                beq monoSid

                cmp #$42
                bne +
                lda #$04        ; Stereo, SID#2 on $D420
                rts
+               cmp #$50
                bne +
                lda #$05        ; Stereo, SID#2 on $D500
                rts
+               cmp #$e0
                bne monoSid
                lda #$06
                rts

monoSid         ldy #$77
                jsr readHeader
                lsr
                lsr
                lsr
                lsr
                and #$03
                beq +
                cmp #$03
                bne ++
+               lda #$01        ; when SID model is unknown or if it supports both, then always force to play on socket SID#1
+               rts
