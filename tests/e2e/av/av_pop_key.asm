; Space-triggered, one-frame white video and SID tone marker.

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
        sta flash_active
        sta space_down
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
; The suite waits for this before it taps Space: a tap sent while the program
; is still loading is not scanned by anything and the pop never happens.
        lda #$a5
        sta $c000
        cli
loop:
        jmp loop

irq:
        lda #1
        sta $d019
        lda flash_active
        beq scan_key
        lda #0
        sta flash_active
        sta $d020
        sta $d021
        sta $d418
        lda #$10
        sta $d404
scan_key:
        lda #$7f
        sta $dc00
        lda $dc01
        and #$10
        bne released
        lda space_down
        bne irq_done
        lda #1
        sta space_down
        sta flash_active
        sta $d020
        sta $d021
        lda #$0f
        sta $d418
        lda #$28
        sta $d400
        lda #0
        sta $d401
        lda #$11
        sta $d404
        jmp irq_done
released:
        lda #0
        sta space_down
irq_done:
        jmp $ea81

flash_active:
        .byte 0
space_down:
        .byte 0
