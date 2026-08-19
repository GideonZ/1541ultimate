; Short SID tone ladder for A/V stream verification.

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
        sta $d020
        sta $d021
        ldx #$18
clear_sid:
        sta $d400,x
        dex
        bpl clear_sid
        lda #$f0
        sta $d406
        lda #0
        sta note_index
        lda #100
        sta frames_left
        lda #<irq
        sta $0314
        lda #>irq
        sta $0315
        lda #$7f
        sta $dc0d
        sta $dd0d
        lda $dc0d
        lda $dd0d
        lda #1
        sta $d01a
        lda #0
        sta $d012
        lda $d011
        and #$7f
        sta $d011
        lda #1
        sta $d019
        cli
loop:
        jmp loop

irq:
        lda #1
        sta $d019
        dec frames_left
        bne irq_done
        ldx note_index
        cpx #15
        bcs silence
        lda freq_lo,x
        sta $d400
        lda freq_hi,x
        sta $d401
        lda colours,x
        sta $d021
        lda #$0f
        sta $d418
        lda #$11
        sta $d404
        inx
        stx note_index
        lda #10
        sta frames_left
        jmp irq_done
silence:
        lda #0
        sta $d418
        lda #$10
        sta $d404
irq_done:
        jmp $ea81

note_index:
        .byte 0
frames_left:
        .byte 0
freq_lo:
        .byte $b3,$c4,$f6,$9d,$0a,$a2,$6c,$67,$6c,$a2,$0a,$9d,$f6,$c4,$b3
freq_hi:
        .byte $08,$09,$0a,$0b,$0d,$0e,$10,$11,$10,$0e,$0d,$0b,$0a,$09,$08
colours:
        .byte 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
