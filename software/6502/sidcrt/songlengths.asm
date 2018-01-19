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

; CONSTANTS
LOCATION_SSL = $b000

; loadSongLengths: Loads the song lengths to specified address.
;                  The song lengths will be placed at $B000 of the cartridge ROM when
;                  an associated SSL file is found during the load of the SID file.
;                  When there is an invalid value located at $B000 or the value
;                  is interpreted as 00:00 seconds then the default time is set.
; input:
;   - XR: number of songs where value $00 means 1 song and value $ff means 256 songs.
;   - $aa: lo-byte of address where the song lengths should be written to.
;   - $ab: hi-byte of address where the song lengths should be written to.
loadSongLengths
                jsr isSslValid
                bne defaultTime

                lda #<LOCATION_SSL
                sta $ac
                lda #>LOCATION_SSL
                sta $ad

                ldy #$00
-               lda ($ac),y
                jsr writeAddress
                iny
                lda ($ac),y
                jsr writeAddress
                iny                 ; after INC Y, Y is always even
                bne +
                inc $ab
                inc $ad
+               dex
                cpx #$ff
                bne -
                rts

isSslValid      lda LOCATION_SSL
                cmp #$a0
                bcs notValid        ; set default time when value is no BCD value
                and #$0f
                cmp #$0a
                bcs notValid        ; set default time when value is no BCD value

                lda LOCATION_SSL
                bne valid
                lda LOCATION_SSL + 1
                beq notValid        ; when value is $0000 then no SSL file is found
valid           lda #$00
                rts
notValid        lda #$01
                rts

defaultTime
                ldy #$00            ; set default song lengths to 5 minutes when no SSL or invalid song length is found
-               lda #$05            ; 5 minutes
                jsr writeAddress
                iny
                lda #$00
                jsr writeAddress
                iny
                bne +
                inc $ab
+               dex
                cpx #$ff
                bne -
                rts

displayCurSongLength
                jsr isSslValid
                bne dontWriteSongLength     ; if there is no ssl available then don't display default time for normal player

                lda SCREEN_LOCATION
                clc
                adc #$03
                sta $ff
                lda #$bd
                sta $fe

                lda SONG_TO_PLAY
                asl
                tax

                inx
                ldy #$02
-               lda LOCATION_SSL,x
                pha
                and #$0f
                clc
                adc #$30
                jsr screenWrite
                pla
                lsr
                lsr
                lsr
                lsr
                clc
                adc #$30
                dec $fe
-               jsr screenWrite
                cpy #$02
                bne +
                dey
                lda #':'
                bne -
+
                dex
                dey
                bpl --
 dontWriteSongLength
                rts
