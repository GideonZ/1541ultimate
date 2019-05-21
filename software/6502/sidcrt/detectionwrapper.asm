;-----------------------------------------------------------------------
; FILE detectionwrapper.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Wrapper of dectection routines for system info and SIDFX support.
;-----------------------------------------------------------------------

; detectSystem
;   input: none
;   output:
;   - AC: system clock type
;       00 = PAL (312 raster lines, 63 cycles per line)
;       01 = NTSC (263 raster lines, 65 cycles per line)
;       02 = NTSC (262 raster lines, 64 cycles per line, old VIC with bug)
;       04 = PAL Drean (312 raster lines, 65 cycles per line)
;   - YR: SID model
;       00 = 8580
;       01 = 6581
;       02 = unknown
;   - XR: SIDFX detection
;       00 = no SIDFX
;       01 = SIDFX found
detectSystem    jsr extraPlayer.codeStart + extraPlayer.detection.detectC64Clock
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

detectNormalSid jsr extraPlayer.codeStart + extraPlayer.detection.detectSidModel
                ldx #$00        ; XR: 0 = no SIDFX
                rts             ; AC: 0 = 8580, 1 = 6581, 2 = unknown

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
                lda #$06        ; Stereo, SID#2 on $DE00
                rts

monoSid         ldy #$77
                jsr readHeader
                lsr
                lsr
                lsr
                lsr
                and #$03        ; supported model: 00: unknown, 01: 6581, 02: 8580, 03: 6581 and 8580
                beq +           ; when SID model is unknown always force to play it on socket SID#1
                eor #$02        ; the rest of the code uses $00 for 8580, so fix that. this also forces using SID#1 when both chips are supported
                cmp #$03        ; the eor messes up the code for 6581, change it back to $01
                bne ++
+               lda #$01
+               rts
