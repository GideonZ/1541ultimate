;-----------------------------------------------------------------------
; FILE keyboard.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Routines for handling key presses for changing song and fast forward.
;   The following keys are supported:
;     <- for fast forward
;     1-0 for selecting sub tune 1 to 10
;     + and - for increasing/decreasing the song selection
;
; Use 64tass version 1.53.1515 or higher to assemble the code
;-----------------------------------------------------------------------

handleKeyboard  ldx #7
-               ldy keyRow,x
                sty $dc00
                lda $dc01
                cmp #$ff
                bne keyPressed
                sta currentKey,x
                dex
                bne -             ; ignore row 1 (x = 0) since those keys are not supported for now

                ldy #0
                jmp fastForward

keyPressed      cmp currentKey,x
                beq notReleased
                sta currentKey,x

                cpx #$06            ; check for row 7 (which is not handled)
                beq skipKeyCheck
                cpx #$05
                beq row6

                cpx #$07            ; check for runstop to exit player and for <- key to fastforward tune
                bne +

                cmp #$7f            ; check if runstop key is pressed
                beq gotoUltimateMenu

                cmp #$fd
                bne +
                ldy #$01
                sty fastForwardOn
fastForward     sty @w $0000
notReleased     rts

+               cmp #$f6
                beq skipKeyCheck
                ; handle keys 0-9
                tay
                and #$f6            ; check for values $fe and $f7
                cmp #$f6
                beq +
                rts

+               txa
                asl
                tax
                tya
                lsr
                lda #$00
                adc tuneSelect,x
                jmp selectSong

selectSong      cmp maxSong
                beq +
                bcs skipKeyCheck
+               sta currentSong
                jmp selectSubTune

row6            cmp #$fe
                beq plusKey
                cmp #$f7
                beq minKey
skipKeyCheck    rts

minKey          dec currentSong
                lda currentSong
                cmp #$ff
                bne +
                lda maxSong
                sta currentSong
+               jmp selectSubTune

plusKey         lda currentSong
                inc currentSong
                cmp maxSong
                bcc +
                lda #0
                sta currentSong
+               jmp selectSubTune

gotoUltimateMenu
                ; TODO implement code to return to Ultimate menu
                inc $d021
                rts

                .section data
keyRow          .byte $fe, $fd, $fb, $f7, $ef, $df, $bf, $7f
currentKey      .byte 0, 0, 0, 0, 0, 0, 0, 0
tuneSelect      .byte 0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 0, 0, 0, 0, 0, 0
fastForwardOn   .byte 0
                .send data