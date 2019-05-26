;-----------------------------------------------------------------------
; FILE keyboard.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2019 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Routines for handling key presses for changing song, fast forward
;   and for returning to Ultimate menu.
;
;   The following keys are supported:
;     <- for fast forward
;     1-0 for selecting sub tune 1 to 10
;     + and - for increasing/decreasing the song selection
;     runstop for going back to Ultimate menu
;     space for pausing/resuming playback
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
                bne -               ; ignore row 1 (x = 0) since those keys are not supported for now

.if INCLUDE_RUNSTOP==1
                lda runStopPressed  ; check if runstop key is pressed and released
                beq +
                lda #$00
                sta runStopPressed
                jmp gotoUltimateMenu
+
.fi
                ldy #0
                jmp fastForward

keyPressed      cmp currentKey,x
                beq notReleased
                sta currentKey,x

                cpx #$06            ; check for row 7 (which is not handled)
                beq skipKeyCheck
                cpx #$01
                beq row2
                cpx #$05
                beq row6

                cpx #$07            ; check for runstop to exit player, <- key to fastforward tune and space to pause
                bne checkNumKeys

.if INCLUDE_RUNSTOP==1
                cmp #$7f            ; check if runstop key is pressed
                bne noRunStop
                lda #$01
                sta runStopPressed
noRunStop
.fi
                ldy cantPause
                bne checkOtherKeys

                cmp #$ef            ; check if space key is pressed
                bne checkOtherKeys

                lda pauseTune
                sta $d418           ; toggle volume off
sid2            sta $d418
sid3            sta $d418
                eor #$1f
                sta pauseTune
pause           sta @w $0000
notReleased     rts

checkOtherKeys  cmp #$fd
                bne checkNumKeys
                ldy #$01
                sty fastForwardOn
fastForward     sty @w $0000
                rts

checkNumKeys    ldy pauseTune
                bne skipKeyCheck

                cmp #$f6
                beq skipKeyCheck
                ; handle keys 0-9
                tay
                and #$f6            ; check for values $fe and $f7
                cmp #$f6
                bne skipKeyCheck

                txa
                asl
                tax
                tya
                lsr
                lda #$00
                adc tuneSelect,x
                cmp maxSong
                beq setCurrentSong
                bcs skipKeyCheck
                jmp setCurrentSong

row2            and #$7f
                cmp #$5f            ; check for S and shift-S key
                bne +
                lda $d011           ; toggle screen on/off
                eor #$10
                sta $d011
+               rts

row6            ldy pauseTune
                bne skipKeyCheck

                cmp #$fe
                beq plusKey
                cmp #$f7
                beq minKey
skipKeyCheck    rts

minKey          dec currentSong
                lda currentSong
                cmp #$ff
                bne +
                lda maxSong
                jmp setCurrentSong

plusKey         lda currentSong
                inc currentSong
                cmp maxSong
                bcc +
                lda #0
setCurrentSong  sta currentSong
+               jmp selectSubTune

.if INCLUDE_RUNSTOP==1
gotoUltimateMenu
                lda $dffd ; Identification register
                cmp #$c9
                bne noUCI

                ; Check if the UCI is busy
                lda $dffc ; Status register
                and #$30  ; State bits
                bne busyUCI ; Temporarily unavailable

                lda #$84  ; Control Target (Target #4 without reply)
                sta $dffd ; Command pipe
                lda #$05  ; Command Freeze
                sta $dffd ; Command pipe
                ; No params
                lda #$01  ; New Command!
                sta $dffc ; Control register

                ; Now wait patiently
-               lda $dffc
                and #$30  ; Status bits
                bne -
                rts

noUCI           inc $d020
                rts
busyUCI         inc $d021
                rts
.fi
                .section data
keyRow          .byte $fe, $fd, $fb, $f7, $ef, $df, $bf, $7f
currentKey      .byte 0, 0, 0, 0, 0, 0, 0, 0
tuneSelect      .byte 0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 0, 0, 0, 0, 0, 0
fastForwardOn   .byte 0
runStopPressed  .byte 0
                .send data
