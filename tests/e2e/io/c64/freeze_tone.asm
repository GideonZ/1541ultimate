; Continuous SID tone, the stimulus for freezer_audio_test.py.
;
; Plays one full-volume sawtooth for as long as the machine runs it, so the
; suite can measure how loud the machine is at any moment without having to
; time anything. Nothing here reads the keyboard or the raster, so it keeps
; playing while the freezer owns the machine.

*=$0801
        .word basic_end
        .word 10
        .byte $9e
        .text format("%4d", start)
        .byte 0
basic_end:
        .word 0

start:
        sei
        lda #$37
        sta $01
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
        cli
loop:
        jmp loop
