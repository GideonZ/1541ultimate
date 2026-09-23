; A bus talker in the C64's own code, for timing that the KERNAL does not produce.
; One transaction: ATN: LISTEN device, secondary; data bytes (EOI on the last one when
; asked); a chosen pause; ATN: UNLISTEN. It follows what a CMD FD-2000 (DOS V1.40,
; $A57B-$A668) and a CMD HD (boot ROM 2.80, $CEF3-$CFCA) send when SWAP is pressed:
; their pause before UNLISTEN is about 40 us, and the KERNAL's is longer.
;
; Mailbox, all set before GO:
;   $c000 GO (1), cleared when done     $c001 READY ($a5 once running)
;   $c002 device                        $c003 secondary byte as sent ($6f, $f2, $62, $e2)
;   $c004 data byte count (0-255)       $c005 flags: bit 7 EOI on the last byte,
;                                                    bit 6 short bits (KERNAL-like)
;   $c006 pause before UNLISTEN, in 5 us steps after the last acknowledgement
;   $c007 result: 0, or the handshake step that timed out or found no device
; Data bytes at $c100. The CMD drives run at 2 MHz, so their delay loops are halved here
; in cycles to keep them the same in microseconds.
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
    sta $c007
    sei
    jsr transaction
    sta $c007
    jsr release_all
    cli
    lda #0
    sta $c000
    jmp idle

transaction
    lda $c002
    ora #$20                ; LISTEN, as $A5AE
    jsr atn_first
    bcs fail
    lda $c003               ; the secondary address, as $A649
    jsr atn_next
    bcs fail
    jsr atn_off             ; $A64E
    ldx #0
    stx last
data
    cpx $c004
    beq unlisten
    stx save_x
    inx
    cpx $c004
    bne not_last
    lda #1
    sta last
    lda $c005
    and #$80
    sta eoi                 ; EOI on the last byte when asked, as $A599
not_last
    ldx save_x
    lda $c100,x
    jsr send
    bcs fail
    lda last
    bne unlisten            ; straight on after the last byte, as the CMD drives do
    ldx save_x
    inx
    jmp data
; From the last acknowledgement to ATN takes about 45 us here, about 40 us on a CMD FD
; ($A65A-$A5BF at 2 MHz), and each pause step adds 5 us.
unlisten
    ldy $c006
    beq atn_now
pause
    dey
    bne pause
atn_now
    lda $dd00
    and #$cf                ; CLK and DATA released
    ora #$08                ; ATN asserted
    sta $dd00
    lda #$3f                ; UNLISTEN, as $A633
    jsr atn_next
    bcs fail
    jsr atn_off             ; $A638: ATN off, 50 us, CLK and DATA released
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
    jsr bit_delay
    ror byte
    bcs one
    jsr data_lo
    jmp clock
one
    jsr data_hi
clock
    jsr clk_hi              ; data valid
    jsr bit_delay
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

; Half a bit: 50 us as the CMD drives' LDA #$14 loop at 2 MHz, or about 20 us, the
; KERNAL's pace, when flag bit 6 asks for short bits.
bit_delay
    bit $c005
    bvs short_bit
d50
    ldy #9
d50l
    dey
    bne d50l
    rts
short_bit
    ldy #2
sbl
    dey
    bne sbl
    rts
; 1 ms, as the FD's $A668.
d1ms
    ldy #200
d1l
    nop
    dey
    bne d1l
    rts

byte    .byte 0
eoi     .byte 0
step    .byte 0
count   .byte 0
save_x  .byte 0
last    .byte 0
