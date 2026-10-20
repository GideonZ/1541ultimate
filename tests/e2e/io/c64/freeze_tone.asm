; Continuous SID tone for freezer_audio_test.py, as a PSID init/play pair.
; init gates one full-volume sawtooth and returns; play does nothing, so the
; note sustains for as long as the player runs.

*=$1000
init:
        lda #0
        ldx #$18
clear_sid:
        sta $d400,x
        dex
        bpl clear_sid
        lda #$b3
        sta $d400
        lda #$08
        sta $d401
        lda #$f0
        sta $d406
        lda #$0f
        sta $d418
        lda #$11
        sta $d404
        rts

; Fixed: freezer_audio_test.py's PSID header names both addresses.
*=$1040
play:
        rts
