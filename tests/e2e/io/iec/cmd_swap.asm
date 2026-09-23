; What a CMD FD-2000 (DOS V1.40, $A57B-$A668) or CMD HD (boot ROM 2.80, $CEF3-$CFCA) sends
; when its SWAP button hands its own number to the drive at 8 or 9, replayed from the C64
; as bus master with the drive's timing (SI-100a, #933):
;   ATN: LISTEN dev, $6F; data: "M-W" $77 $00 $02 <listen> <talk> (EOI); ATN: UNLISTEN.
; Both drives run at 2 MHz, so their delay loops are halved here in cycles to keep them the
; same in microseconds. The part that matters is the end: UNLISTEN raises ATN about 20 us
; after the last byte is acknowledged, well inside the time the KERNAL at 1 MHz takes.
; Mailbox: $c000 go, $c001 ready ($a5), $c002 device, $c003 listen byte, $c004 talk byte,
; $c005 result: 0, or the handshake step that timed out or found no device.
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
idle
    lda $c000
    beq idle
    lda #0
    sta $c005
    sei
    jsr swap
    sta $c005
    jsr release_all
    cli
    lda #0
    sta $c000
    jmp idle

swap
    lda $c002
    ora #$20                ; LISTEN, as $A5AE
    jsr atn_first
    bcs fail
    lda #$6f                ; secondary: data to channel 15, no OPEN, as $A649
    jsr atn_next
    bcs fail
    jsr atn_off             ; $A64E
    ldx #0
cmd
    lda template,x
    stx save_x
    jsr send
    bcs fail
    ldx save_x
    inx
    cpx #6
    bcc cmd
    lda $c003
    jsr send
    bcs fail
    lda #$80                ; EOI on the last byte, as $A599
    sta eoi
    lda $c004
    jsr send
    bcs fail
    lda #$3f                ; UNLISTEN, as $A633
    jsr atn_first
    bcs fail
    jsr atn_off             ; $A638: ATN off, 50 µs, CLK and DATA released
    jsr d50
    lda #0
    rts
fail
    lda step
    rts

; $A5B0: release DATA, release everything and assert ATN, then as atn_next.
atn_first
    pha
    jsr data_hi
    lda $dd00
    and #$cf                ; CLK and DATA released
    ora #$08                ; ATN asserted
    sta $dd00
    pla
; $A5C6: CLK low, DATA released, 1 ms, then the byte without EOI.
atn_next
    pha
    jsr clk_lo
    jsr data_hi
    jsr d1ms
    lda #0
    sta eoi
    pla
; $A5D7: one byte.
send
    sta byte
    lda #1
    sta step
    jsr data_hi
    lda $dd00
    bmi nodev               ; DATA not held by any listener: no device
    jsr clk_hi              ; ready to send
    bit eoi
    bpl no_eoi
    lda #2
    sta step
    jsr wait_data_hi
    bcs nodev
    lda #3
    sta step
    jsr wait_data_lo        ; the listener's EOI acknowledgement
    bcs nodev
no_eoi
    lda #4
    sta step
    jsr wait_data_hi        ; listener ready for data
    bcs nodev
    jsr clk_lo
    ldx #8
bit_loop
    jsr d50
    ror byte
    bcs one
    jsr data_lo
    jmp clock
one
    jsr data_hi
clock
    jsr clk_hi              ; data valid
    jsr d50
    lda $dd00
    and #$df                ; DATA released
    ora #$10                ; CLK low, in the same write, as $A61A
    sta $dd00
    dex
    bne bit_loop
    lda #5
    sta step
    jsr wait_data_lo        ; the byte acknowledged
    bcs nodev
    clc
    rts
nodev
    sec
    rts

atn_off
    lda $dd00
    and #$f7
    sta $dd00
    rts
release_all
    lda $dd00
    and #$c7
    sta $dd00
    rts
data_hi
    lda $dd00
    and #$df
    sta $dd00
    rts
data_lo
    lda $dd00
    ora #$20
    sta $dd00
    rts
clk_hi
    lda $dd00
    and #$ef
    sta $dd00
    rts
clk_lo
    lda $dd00
    ora #$10
    sta $dd00
    rts

; Waits for DATA with a timeout of about 0.3 s, carry set on timeout.
wait_data_hi
    ldy #0
    sty count
wh1
    lda $dd00
    bmi ok
    dey
    bne wh1
    dec count
    bne wh1
    sec
    rts
wait_data_lo
    ldy #0
    sty count
wl1
    lda $dd00
    bpl ok
    dey
    bne wl1
    dec count
    bne wl1
    sec
    rts
ok
    clc
    rts

; 50 µs at 1 MHz, as the FD's LDA #$14 loop at 2 MHz.
d50
    ldy #9
d50l
    dey
    bne d50l
    rts
; 1 ms, as the FD's $A668.
d1ms
    ldy #200
d1l
    nop
    dey
    bne d1l
    rts

template .byte $4d, $2d, $57, $77, $00, $02
byte    .byte 0
eoi     .byte 0
step    .byte 0
count   .byte 0
save_x  .byte 0
