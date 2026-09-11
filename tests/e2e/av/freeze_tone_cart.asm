; Ultimax cartridge counterpart of freeze_tone.asm. The reset vectors are in
; cartridge ROM, so a C64_START_CART transition starts the tone without a PRG.

*=$e000
reset:
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

*=$fffc
        .word reset
        .word reset
