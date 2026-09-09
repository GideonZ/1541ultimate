; KERNAL IEC transactions on real C64 hardware. Host mailbox at $c000:
; GO (1=open, 2=write, 3=read to EOI, 4=close, 5=read a given count), READY,
; device, channel, byte count, KERNAL status, carry/error, secondary address.
; Input/output bytes at $c100. The channel is the logical file number, which the
; KERNAL refuses to be zero, so OPEN takes the secondary address separately.
;
; Operation 5 reads the number of bytes the host asked for and stops there, for
; channels that only signal EOI at the end of a fixed size block, such as the
; direct access buffer a block command fills. Operation 3 reads to EOI instead
; and reports an error past 254 bytes.
;
; Shared by every suite under tests/e2e/io/iec; see iec_agent.py.
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
    cmp #5
    beq do_readn
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
    ldy $c007
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
    lda #254
    sta limit
    lda #$ff
    sta capped
    jmp read_start
do_readn
    lda $c004
    sta limit
    lda #0
    sta capped
read_start
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
    cmp limit
    bne read_loop
    lda capped
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
limit .byte 0
capped .byte 0
