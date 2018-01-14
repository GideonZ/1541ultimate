;-----------------------------------------------------------------------
; FILE songlengths.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Reads the song lengths from the associated SSL file.
;   More information about SSL files can be found here:
;     HVSC/C64Music/DOCUMENTS/Songlengths.faq
;-----------------------------------------------------------------------

; loadSongLengths: loads the song lengths to specified address
; input:
;   - XR: number of songs where value $00 means 1 song and value $ff means 256 songs.
;   - $aa: lo-byte of address where the song lengths should be written to.
;   - $ab: hi-byte of address where the song lengths should be written to.
loadSongLengths
; TODO read the songlengths from the SSL file, for now all songs are defaulted to 5 minutes
                ldy #$00
-               lda #$05            ; 5 minutes
                jsr writeAddress
                iny
                lda #$00
                jsr writeAddress
                iny

                dex
                cpx #$ff
                bne -
                rts