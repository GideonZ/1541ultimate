; KERNAL IEC transactions on real C64 hardware. Host mailbox at $c000:
; GO (1=open, 2=write, 3=read to EOI, 4=close), READY, device, channel,
; byte count, KERNAL status, carry/error. Input/output bytes at $c100.
; OPEN uses channel as both logical file number and secondary address.
* = $0801
    .word basic_end, 10
    .byte $9e
    .text "2061"
    .byte 0
basic_end
    .word 0
    lda #0
    sta $c000
    lda #$a5
    sta $c001
wait
    lda $c000
    beq wait
    lda #0
    sta $c005
    sta $c006
    sta $90
    lda $c000
    cmp #1
    beq do_open
    cmp #2
    beq do_write
    cmp #3
    beq do_read
    lda $c003
    jsr $ffc3
    jmp done
do_open
    lda $c004
    ldx #<$c100
    ldy #>$c100
    jsr $ffbd
    lda $c003
    ldx $c002
    ldy $c003
    jsr $ffba
    jsr $ffc0
    bcc done
error
    sta $c006
    jmp done
do_write
    ldx $c003
    jsr $ffc9
    bcs error
    lda #0
    sta index
write_loop
    ldx index
    cpx $c004
    beq done
    lda $c100,x
    jsr $ffd2
    inc index
    jmp write_loop
do_read
    ldx $c003
    jsr $ffc6
    bcs error
    lda #0
    sta index
read_loop
    jsr $ffcf
    ldx index
    sta $c100,x
    inc index
    jsr $ffb7
    bne read_done
    lda index
    cmp #254
    bne read_loop
    lda #$ff
    sta $c006
read_done
    sta $c005
    lda index
    sta $c004
done
    jsr $ffcc
    lda $c005
    bne finish
    jsr $ffb7
    sta $c005
finish
    lda #0
    sta $c000
    jmp wait
index .byte 0
