; Init and play for sid_top_of_memory_test.py. The suite fills the rest of the
; image, up to the last byte under test, with bytes the player must not touch.
; play counts its calls, so the suite can see the player is calling it. init
; records the memory configuration it was called with.

*=$1000
init:
        lda $01
        sta bank
        lda #0
        sta calls
        sta calls+1
        rts

*=$1040
play:
        inc calls
        bne +
        inc calls+1
+       rts

*=$1080
calls:
        .word 0
bank:
        .byte 0
