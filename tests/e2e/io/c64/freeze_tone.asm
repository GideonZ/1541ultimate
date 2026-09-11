; Continuous SID tone for the freezer mixer checks in stream_test.py.
;
; One source, two builds, chosen by -D CARTRIDGE=1:
;
;   CARTRIDGE = 0   a BASIC-stub PRG at $0801, started with runners:run_prg.
;   CARTRIDGE = 1   an 8 KiB Ultimax ROM image at $E000 whose reset vector runs
;                   the same code, started with runners:run_crt. The reset
;                   vectors are in cartridge ROM, so starting the cartridge
;                   starts the tone with no PRG and no keyboard involved.
;
; The tone itself is identical in both, which is what lets the suite compare
; the peak it measures after a cartridge start against the baseline it measured
; from the PRG.

        .weak
CARTRIDGE = 0
        .endweak

        .if CARTRIDGE
*=$e000
        .else
*=$0801
        .word basic_end
        .word 10
        .byte $9e
        .text format("%4d", start)
        .byte 0
basic_end:
        .word 0
        .endif

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

        .if CARTRIDGE
; RESET and IRQ/BRK both enter at the top. In Ultimax mode there is no stack
; page, so nothing here may use one, and an interrupt that did arrive would
; restart the tone rather than run off into unmapped memory.
*=$fffc
        .word start
        .word start
        .endif
