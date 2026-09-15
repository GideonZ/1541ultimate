; ------------------------------------------------------------
; Mouse listener for control port 1
; ------------------------------------------------------------
;
; Follows a 1351 mouse with a Micromys wheel on control port 1, the
; joystick on control port 2 and the keyboard, and keeps what it saw
; in a fixed RAM block, so a test can read it over REST, and on screen,
; so a person can watch it.
;
; Inputs, as a 1351 driver, a Micromys driver and a keyboard scan read
; them:
;   SID POTX/POTY with port 1's paddle group selected: position
;   CIA1 port B ($DC01), active low, port 1:
;     bit 4 = left button     bit 0 = right button
;     bit 1 = middle button   bit 2 = wheel up, bit 3 = wheel down
;   CIA1 port A ($DC00), active low, port 2:
;     bit 0 = up, bit 1 = down, bit 2 = left, bit 3 = right, bit 4 = fire
;   the keyboard matrix, columns on port A, rows on port B
;
; Once per frame, as a driver in a raster interrupt does, the listener
; samples the position and scans the keyboard. Each position sample
; adds the change of the 7-bit POT value, taken as a signed number
; from -64 to +63, to a 16-bit total. The totals are in POT counts,
; twice the resolution a 1351 driver shows, which drops the lowest
; bit. Y grows downwards, so Y moves opposite to POTY.
;
; The smallest and largest change of each POT between two frames are
; kept as well. While the mouse moves one way, a change the other way,
; or one of 64 or more, which reads as the other way, is jitter.
;
; The joystick lines of both ports are sampled on every pass of the
; main loop, so each button press and each wheel pulse is counted once.
;
; The keyboard scan cannot see a key in a row that a port 1 line holds
; low, and it skips a frame while any port 2 line is active, because
; that line selects a whole column. A key that is pressed and released
; entirely inside such a time is not counted.
;
; Result block, all values little-endian:
;   $C000  READY    $A5 once the listener runs
;   $C001  RESET    write non-zero: zero the totals and counters and
;                   take the current POT values as the new origin;
;                   the listener writes 0 back when done
;   $C002  X        signed 16-bit position
;   $C004  Y        signed 16-bit position
;   $C006  BUTTONS  held now: bit 0 left, bit 1 middle, bit 2 right
;   $C007  LEFT     presses of the left button
;   $C008  MIDDLE   presses of the middle button
;   $C009  RIGHT    presses of the right button
;   $C00A  WHEELUP  wheel up pulses
;   $C00B  WHEELDN  wheel down pulses
;   $C00C  FRAMES   16-bit count of position samples
;   $C00E  POTX     last POTX
;   $C00F  POTY     last POTY
;   $C010  LINES    last port 1 lines, 1 = active
;   $C011  MINDPX   smallest signed POTX change between two frames
;   $C012  MAXDPX   largest signed POTX change between two frames
;   $C013  MINDPY   smallest signed POTY change between two frames
;   $C014  MAXDPY   largest signed POTY change between two frames
;   $C015  PORT2    last port 2 lines, 1 = active
;   $C016  P2UP     presses of port 2 up, then down, left, right and
;                   fire at $C017-$C01A
;   $C01B  CRSRUP   cursor up presses: CRSR DOWN with a shift key,
;                   then cursor down, left and right at $C01C-$C01E
;   $C01F  KEYSHELD keys down in the last scan, shift keys included
;   $C020  KEYCOUNT presses of each key, 64 bytes, at
;                   $C020 + column * 8 + row, where the column is
;                   the bit of $DC00 and the row the bit of $DC01
;   $C060  MATRIX   the last scan, 8 bytes, one per column, 1 = down
;
; Between samples port A drives no keyboard column: with $DC00 at $40, bit
; 7 would select column 7 and its keys would read as port 1 lines. Port 1's
; paddle group is selected only for the POT read itself. The Ultimate's
; mouse emulation answers at once; a real SID would need the selection to
; settle for a sample period first.
;
; The listener runs with interrupts disabled and does not call the
; KERNAL. RUN/STOP-RESTORE or a machine reset leaves it.
;
; ------------------------------------------------------------

READY     = $C000
RESET     = $C001
POS_X     = $C002
POS_Y     = $C004
BUTTONS   = $C006
LEFT      = $C007
MIDDLE    = $C008
RIGHT     = $C009
WHEELUP   = $C00A
WHEELDN   = $C00B
FRAMES    = $C00C
POTX_LAST = $C00E
POTY_LAST = $C00F
LINES     = $C010
MINDPX    = $C011
MAXDPX    = $C012
MINDPY    = $C013
MAXDPY    = $C014
PORT2     = $C015
P2COUNT   = $C016               ; up, down, left, right, fire
CRSRUP    = $C01B
CRSRDOWN  = $C01C
CRSRLEFT  = $C01D
CRSRRIGHT = $C01E
KEYSHELD  = $C01F
KEYCOUNT  = $C020
MATRIX    = $C060
COUNTS_END = $C060              ; RESET zeroes MINDPX up to here, and POS_X to FRAMES

; Listener state, after the result block.
OLD_X     = $C080
OLD_Y     = $C081
PREV1     = $C082
PREV2     = $C083
NEWBITS   = $C084
DELTA     = $C085
EXTEND    = $C086
LASTTOP   = $C087
DRAWPHASE = $C090
DRAWEND   = $C091
NUM       = $C088               ; 16-bit number to print
DIGIT     = $C08A
ROWMASK   = $C08B
IDLE1     = $C092
SCAN      = $C098               ; 8 bytes, the scan being checked
HELD      = $C093
ROWS      = $C08C
KEYINDEX  = $C08D
NEWCOL0   = $C08E
SAVEX     = $C08F

SCREEN    = $0400
COLOUR    = $D800
PTR       = $FB                 ; zero page pointers
PTR2      = $FD

POTX      = $D419
POTY      = $D41A
CIA1_PRA  = $DC00
CIA1_PRB  = $DC01
CIA1_DDRA = $DC02
CIA1_DDRB = $DC03
VIC_CTRL1 = $D011

LINE_RIGHT  = %00000001
LINE_MIDDLE = %00000010
LINE_LEFT   = %00010000

SPRITE_BLOCK = 13               ; shape at $0340

        * = $0801
        .word basic_end, 10
        .byte $9e
        .text "2061"
        .byte 0
basic_end
        .word 0

start
        sei
        lda #$00
        sta READY
        sta LASTTOP
        sta CIA1_DDRB           ; port B reads the port 1 lines and the rows
        jsr select_no_column
        jsr draw_screen
        jsr setup_sprite
        jsr reset_state
        lda #$A5
        sta READY

main_loop
        lda RESET
        beq no_reset
        jsr reset_state
no_reset
        jsr sample_port1_lines
        jsr sample_port2_lines
        ; Once per frame: when the raster leaves the lines above 255, the
        ; top of a new frame is next.
        lda VIC_CTRL1
        and #$80
        tax
        eor LASTTOP
        beq main_loop
        stx LASTTOP
        txa
        bne main_loop           ; entered the lines above 255
        jsr sample_position
        jsr scan_keyboard
        jsr place_sprite
        jsr draw_values
        jmp main_loop

; Port A drives only bits 6 and 7, both high: no keyboard column is
; selected, and bits 0-4 read port 2.
select_no_column
        lda #$C0
        sta CIA1_DDRA
        lda #$FF
        sta CIA1_PRA
        rts

; ------------------------------------------------------------
; Sampling
; ------------------------------------------------------------

sample_port1_lines
        lda CIA1_PRB
        eor #$FF
        and #$1F
        sta LINES
        lda PREV1
        eor #$FF
        and LINES
        sta NEWBITS
        lda LINES
        sta PREV1
        lsr NEWBITS             ; bit 0: right
        bcc +
        inc RIGHT
+       lsr NEWBITS             ; bit 1: middle
        bcc +
        inc MIDDLE
+       lsr NEWBITS             ; bit 2: wheel up
        bcc +
        inc WHEELUP
+       lsr NEWBITS             ; bit 3: wheel down
        bcc +
        inc WHEELDN
+       lsr NEWBITS             ; bit 4: left
        bcc +
        inc LEFT
+       ldx #$00
        lda LINES
        and #LINE_LEFT
        beq +
        inx
+       lda LINES
        and #LINE_MIDDLE
        beq +
        txa
        ora #$02
        tax
+       lda LINES
        and #LINE_RIGHT
        beq +
        txa
        ora #$04
        tax
+       stx BUTTONS
        rts

sample_port2_lines
        lda CIA1_PRA
        eor #$FF
        and #$1F
        sta PORT2
        lda PREV2
        eor #$FF
        and PORT2
        sta NEWBITS
        lda PORT2
        sta PREV2
        ldx #$00
-       lsr NEWBITS
        bcc +
        inc P2COUNT,x
+       inx
        cpx #$05
        bne -
        rts

sample_position
        inc FRAMES
        bne +
        inc FRAMES+1
+       lda #$40                ; bit 6: port 1's paddle group
        sta CIA1_PRA
        lda POTY
        sta POTY_LAST
        lda POTX
        sta POTX_LAST
        lda #$FF
        sta CIA1_PRA
        lda POTX_LAST
        tax
        sec
        sbc OLD_X
        stx OLD_X
        jsr signed7
        ldx #MINDPX - MINDPX
        jsr track_step
        clc
        lda POS_X
        adc DELTA
        sta POS_X
        lda POS_X+1
        adc EXTEND
        sta POS_X+1
        lda POTY_LAST
        tax
        sec
        sbc OLD_Y
        stx OLD_Y
        jsr signed7
        ldx #MINDPY - MINDPX
        jsr track_step
        sec                     ; the POT value falls as the mouse moves down
        lda POS_Y
        sbc DELTA
        sta POS_Y
        lda POS_Y+1
        sbc EXTEND
        sta POS_Y+1
        rts

; A = change of a POT value. DELTA/EXTEND = the low 7 bits as a signed
; number from -64 to +63.
signed7
        and #$7F
        ldx #$00
        cmp #$40
        bcc +
        ora #$80
        dex
+       sta DELTA
        stx EXTEND
        rts

; Keep DELTA as the new minimum at MINDPX,x and maximum at MAXDPX,x.
track_step
        lda DELTA
        sec
        sbc MINDPX,x
        bvc min_sign
        eor #$80
min_sign
        bpl check_max           ; DELTA >= minimum
        lda DELTA
        sta MINDPX,x
check_max
        lda MAXDPX,x
        sec
        sbc DELTA
        bvc max_sign
        eor #$80
max_sign
        bpl step_done           ; maximum >= DELTA
        lda DELTA
        sta MAXDPX,x
step_done
        rts

; ------------------------------------------------------------
; Keyboard
; ------------------------------------------------------------

scan_keyboard
        ; The lines as they are with no column selected. A port 2 line
        ; selects a whole column, so no scan while one is active.
        lda CIA1_PRA
        eor #$FF
        and #$1F
        beq +
        rts
+       lda CIA1_PRB
        eor #$FF
        and #$1F
        sta IDLE1
        lda #$FF
        sta CIA1_DDRA
        ldx #$00
-       lda column_select,x
        sta CIA1_PRA
        lda CIA1_PRB
        eor #$FF
        sta SCAN,x
        inx
        cpx #$08
        bne -
        jsr select_no_column
        ; A line that changed during the scan may have read as keys in some
        ; columns and not in others, so such a scan is not used.
        lda CIA1_PRA
        eor #$FF
        and #$1F
        beq +
        rts
+       lda CIA1_PRB
        eor #$FF
        and #$1F
        cmp IDLE1
        beq +
        rts
+       lda IDLE1
        eor #$FF
        sta ROWMASK             ; rows no port 1 line holds low
        lda #$00
        sta HELD
        tax
apply_column
        ; A row a port 1 line holds low reads as down in every column, so it
        ; keeps the state it had before the line went active.
        lda SCAN,x
        and ROWMASK
        sta ROWS
        lda MATRIX,x
        and IDLE1
        ora ROWS
        sta ROWS
        lda MATRIX,x
        eor #$FF
        and ROWS
        sta NEWBITS
        cpx #$00
        bne +
        sta NEWCOL0
+       lda ROWS
        sta MATRIX,x
        txa
        asl
        asl
        asl
        sta KEYINDEX
        stx SAVEX
        ldy #$08
count_rows
        lsr ROWS
        bcc +
        inc HELD
+       lsr NEWBITS
        bcc +
        ldx KEYINDEX
        inc KEYCOUNT,x
+       inc KEYINDEX
        dey
        bne count_rows
        ldx SAVEX
        inx
        cpx #$08
        bne apply_column
        lda HELD
        sta KEYSHELD
        ; Cursor keys: CRSR DOWN is column 0 row 7, CRSR RIGHT column 0
        ; row 2, and a shift key held with either reverses it.
        ldx #$00
        lda MATRIX+1
        and #$80                ; left shift, column 1 row 7
        bne shifted
        lda MATRIX+6
        and #$10                ; right shift, column 6 row 4
        beq not_shifted
shifted ldx #$01
not_shifted
        lda NEWCOL0
        and #$80
        beq check_right
        txa
        bne crsr_up
        inc CRSRDOWN
        jmp check_right
crsr_up inc CRSRUP
check_right
        lda NEWCOL0
        and #$04
        beq scan_done
        txa
        bne crsr_left
        inc CRSRRIGHT
        rts
crsr_left
        inc CRSRLEFT
scan_done
        rts

column_select .byte $FE, $FD, $FB, $F7, $EF, $DF, $BF, $7F

reset_state
        lda #$00
        ldx #POTX_LAST - POS_X - 1
-       sta POS_X,x
        dex
        bpl -
        ldx #COUNTS_END - MINDPX - 1
-       sta MINDPX,x
        dex
        bpl -
        lda #$40                ; bit 6: port 1's paddle group
        sta CIA1_PRA
        lda POTX
        sta OLD_X
        sta POTX_LAST
        lda POTY
        sta OLD_Y
        sta POTY_LAST
        lda #$FF
        sta CIA1_PRA
        lda CIA1_PRB
        eor #$FF
        and #$1F
        sta PREV1
        sta LINES
        lda CIA1_PRA
        eor #$FF
        and #$1F
        sta PREV2
        sta PORT2
        lda #$00
        sta RESET
        rts

; ------------------------------------------------------------
; Screen
; ------------------------------------------------------------

draw_screen
        lda #$06
        sta $D020
        sta $D021
        ldx #$00
-       lda #$20
        sta SCREEN,x
        sta SCREEN+$100,x
        sta SCREEN+$200,x
        sta SCREEN+$2E8,x
        lda #$01
        sta COLOUR,x
        sta COLOUR+$100,x
        sta COLOUR+$200,x
        sta COLOUR+$2E8,x
        inx
        bne -
        lda #<labels
        sta PTR
        lda #>labels
        sta PTR+1
        lda #<(SCREEN+40)
        sta PTR2
        lda #>(SCREEN+40)
        sta PTR2+1
        ldy #$00
copy_label
        lda (PTR),y
        beq copy_done
        sta (PTR2),y
        inc PTR
        bne +
        inc PTR+1
+       inc PTR2
        bne copy_label
        inc PTR2+1
        bne copy_label
copy_done
        rts

; Row and column of each value on screen.
ROW_X         = 3*40+3
ROW_Y         = 3*40+15
ROW_MINDPX    = 4*40+8
ROW_MAXDPX    = 4*40+13
ROW_MINDPY    = 4*40+21
ROW_MAXDPY    = 4*40+26
ROW_LEFT      = 7*40+13
ROW_BTN       = 7*40+1
ROW_MIDDLE    = 8*40+13
ROW_RIGHT     = 9*40+13
ROW_WHEELUP   = 10*40+13
ROW_WHEELDN   = 10*40+25
ROW_CRSRUP    = 12*40+13
ROW_CRSRDOWN  = 12*40+25
ROW_CRSRLEFT  = 13*40+13
ROW_CRSRRIGHT = 13*40+25
ROW_KEYSHELD  = 14*40+13
ROW_P2UP      = 16*40+13
ROW_P2DOWN    = 16*40+25
ROW_P2LEFT    = 17*40+13
ROW_P2RIGHT   = 17*40+25
ROW_P2FIRE    = 18*40+13
ROW_POTX      = 20*40+9
ROW_POTY      = 20*40+13
ROW_LINES1    = 20*40+24
ROW_LINES2    = 20*40+28
ROW_FRAMES    = 21*40+9

; A full redraw takes most of a frame, which would make the listener miss
; samples, so each frame redraws one of four parts.
draw_values
        inc DRAWPHASE
        lda DRAWPHASE
        and #$03
        bne +
        jmp draw_position
+       cmp #$01
        bne +
        jmp draw_steps
+       cmp #$02
        bne +
        ldx #$00
        ldy #BYTE_FIELDS_FIRST
        jmp draw_bytes
+       ldx #BYTE_FIELDS_FIRST
        ldy #BYTE_FIELDS
        jsr draw_bytes
        jmp draw_buttons

draw_position
        lda POS_X
        sta NUM
        lda POS_X+1
        sta NUM+1
        ldy #<ROW_X
        ldx #>ROW_X
        jsr print_signed
        lda POS_Y
        sta NUM
        lda POS_Y+1
        sta NUM+1
        ldy #<ROW_Y
        ldx #>ROW_Y
        jsr print_signed
        lda FRAMES
        sta NUM
        lda FRAMES+1
        sta NUM+1
        ldy #<ROW_FRAMES
        ldx #>ROW_FRAMES
        jmp print_unsigned

draw_steps
        lda MINDPX
        ldy #<ROW_MINDPX
        ldx #>ROW_MINDPX
        jsr print_signed_byte
        lda MAXDPX
        ldy #<ROW_MAXDPX
        ldx #>ROW_MAXDPX
        jsr print_signed_byte
        lda MINDPY
        ldy #<ROW_MINDPY
        ldx #>ROW_MINDPY
        jsr print_signed_byte
        lda MAXDPY
        ldy #<ROW_MAXDPY
        ldx #>ROW_MAXDPY
        jsr print_signed_byte
        lda POTX_LAST
        ldy #<ROW_POTX
        ldx #>ROW_POTX
        jsr print_hex
        lda POTY_LAST
        ldy #<ROW_POTY
        ldx #>ROW_POTY
        jsr print_hex
        lda LINES
        ldy #<ROW_LINES1
        ldx #>ROW_LINES1
        jsr print_hex
        lda PORT2
        ldy #<ROW_LINES2
        ldx #>ROW_LINES2
        jmp print_hex

; The counters from field X up to, not including, field Y.
draw_bytes
        sty DRAWEND
        stx SAVEX
draw_byte
        ldx SAVEX
        cpx DRAWEND
        beq bytes_done
        lda byte_sources_lo,x
        sta PTR2
        lda byte_sources_hi,x
        sta PTR2+1
        ldy #$00
        lda (PTR2),y
        pha
        lda byte_rows_lo,x
        tay
        lda byte_rows_hi,x
        tax
        pla
        jsr print_byte
        inc SAVEX
        jmp draw_byte
bytes_done
        rts

; Reverse a button's label while it is held.
draw_buttons
        ldx #$00
button_label
        lda button_rows_lo,x
        sta PTR
        lda button_rows_hi,x
        sta PTR+1
        ldy #$05
button_char
        lda (PTR),y
        and #$7F
        pha
        lda BUTTONS
        and button_bits,x
        beq button_plain
        pla
        ora #$80
        pha
button_plain
        pla
        sta (PTR),y
        dey
        bpl button_char
        inx
        cpx #$03
        bne button_label
        rts

; The counters printed as three digits: where each is kept, where it goes.
byte_sources_lo .byte <LEFT, <MIDDLE, <RIGHT, <WHEELUP, <WHEELDN
                .byte <CRSRUP, <CRSRDOWN, <CRSRLEFT, <CRSRRIGHT
                .byte <P2COUNT, <(P2COUNT+1), <(P2COUNT+2), <(P2COUNT+3), <(P2COUNT+4)
                .byte <KEYSHELD
byte_sources_hi .byte >LEFT, >MIDDLE, >RIGHT, >WHEELUP, >WHEELDN
                .byte >CRSRUP, >CRSRDOWN, >CRSRLEFT, >CRSRRIGHT
                .byte >P2COUNT, >(P2COUNT+1), >(P2COUNT+2), >(P2COUNT+3), >(P2COUNT+4)
                .byte >KEYSHELD
byte_rows_lo    .byte <ROW_LEFT, <ROW_MIDDLE, <ROW_RIGHT, <ROW_WHEELUP, <ROW_WHEELDN
                .byte <ROW_CRSRUP, <ROW_CRSRDOWN, <ROW_CRSRLEFT, <ROW_CRSRRIGHT
                .byte <ROW_P2UP, <ROW_P2DOWN, <ROW_P2LEFT, <ROW_P2RIGHT, <ROW_P2FIRE
                .byte <ROW_KEYSHELD
byte_rows_hi    .byte >ROW_LEFT, >ROW_MIDDLE, >ROW_RIGHT, >ROW_WHEELUP, >ROW_WHEELDN
                .byte >ROW_CRSRUP, >ROW_CRSRDOWN, >ROW_CRSRLEFT, >ROW_CRSRRIGHT
                .byte >ROW_P2UP, >ROW_P2DOWN, >ROW_P2LEFT, >ROW_P2RIGHT, >ROW_P2FIRE
                .byte >ROW_KEYSHELD
BYTE_FIELDS = 15
BYTE_FIELDS_FIRST = 8

; Y/X = screen offset. Sets PTR to the screen address.
set_ptr
        tya
        clc
        adc #<SCREEN
        sta PTR
        txa
        adc #>SCREEN
        sta PTR+1
        ldy #$00
        rts

; NUM as a sign and five digits.
print_signed
        jsr set_ptr
        lda #$2B                ; '+'
        ldx NUM+1
        bpl +
        sec
        lda #$00
        sbc NUM
        sta NUM
        lda #$00
        sbc NUM+1
        sta NUM+1
        lda #$2D                ; '-'
+       sta (PTR),y
        iny
        jmp print_digits

; A as a sign and three digits.
print_signed_byte
        sta NUM
        lda #$00
        sta NUM+1
        jsr set_ptr
        lda #$2B                ; '+'
        ldx NUM
        bpl +
        sec
        lda #$00
        sbc NUM
        sta NUM
        lda #$2D                ; '-'
+       sta (PTR),y
        iny
        ldx #$02
        jmp print_digits_from

; NUM as five digits.
print_unsigned
        jsr set_ptr
print_digits
        ldx #$00
; Digits from power X (0 = ten thousands) down to units.
print_digits_from
        lda #$30
        sta DIGIT
digit_subtract
        lda NUM
        sec
        sbc powers_lo,x
        pha
        lda NUM+1
        sbc powers_hi,x
        bcc digit_done
        sta NUM+1
        pla
        sta NUM
        inc DIGIT
        bne digit_subtract
digit_done
        pla
        lda DIGIT
        sta (PTR),y
        iny
        inx
        cpx #$05
        bne print_digits_from
        rts

; A as three digits.
print_byte
        sta NUM
        lda #$00
        sta NUM+1
        jsr set_ptr
        ldx #$02
        jmp print_digits_from

; A as "$hh".
print_hex
        pha
        jsr set_ptr
        lda #$24                ; '$'
        sta (PTR),y
        iny
        pla
        pha
        lsr
        lsr
        lsr
        lsr
        tax
        lda hex_digits,x
        sta (PTR),y
        iny
        pla
        and #$0F
        tax
        lda hex_digits,x
        sta (PTR),y
        rts

; ------------------------------------------------------------
; Pointer sprite. Position 0,0 is the middle of the screen, 160,100 in
; screen pixels, so a move either way shows; the pointer stops at the edges.
; ------------------------------------------------------------

setup_sprite
        ldx #20
-       lda arrow,x
        sta $0340,x
        dex
        bpl -
        lda #$00
        ldx #$00
-       sta $0355,x             ; clear the rest of the 24x21 block
        inx
        cpx #$2A
        bne -
        lda #SPRITE_BLOCK
        sta $07F8
        lda #$01
        sta $D027
        sta $D015
        rts

place_sprite
        ; x: POS_X + 160, clamped to 0..319, plus the 24-pixel left border
        clc
        lda POS_X
        adc #<160
        sta NUM
        lda POS_X+1
        adc #>160
        sta NUM+1
        bvs x_overflow
        bmi x_low
        cmp #>320
        bcc x_ok
        bne x_high
        lda NUM
        cmp #<320
        bcc x_ok
x_high  lda #<319
        ldx #>319
        jmp x_set
x_overflow                      ; only a large positive position overflows
        jmp x_high
x_low   lda #$00
        tax
        jmp x_set
x_ok    lda NUM
        ldx NUM+1
x_set   clc
        adc #24
        sta $D000
        txa
        adc #$00
        sta $D010
        ; y: POS_Y + 100, clamped to 0..199, plus the 50-line top border
        clc
        lda POS_Y
        adc #100
        sta NUM
        lda POS_Y+1
        adc #$00
        sta NUM+1
        bvs y_overflow
        bmi y_low
        bne y_high
        lda NUM
        cmp #200
        bcc y_set
y_high  lda #199
        jmp y_set
y_overflow                      ; only a large positive position overflows
        jmp y_high
y_low   lda #$00
y_set   clc
        adc #50
        sta $D001
        rts

; ------------------------------------------------------------
; Data
; ------------------------------------------------------------

powers_lo   .byte <10000, <1000, <100, <10, <1
powers_hi   .byte >10000, >1000, >100, >10, >1
hex_digits  .enc "screen"
            .text "0123456789ABCDEF"
button_bits .byte $01, $02, $04
button_rows_lo .byte <(SCREEN+ROW_BTN), <(SCREEN+ROW_BTN+40), <(SCREEN+ROW_BTN+80)
button_rows_hi .byte >(SCREEN+ROW_BTN), >(SCREEN+ROW_BTN+40), >(SCREEN+ROW_BTN+80)

arrow   .byte %11111100, %00000000, %00000000
        .byte %11111000, %00000000, %00000000
        .byte %11110000, %00000000, %00000000
        .byte %11111000, %00000000, %00000000
        .byte %11011100, %00000000, %00000000
        .byte %10001110, %00000000, %00000000
        .byte %00000111, %00000000, %00000000

; 40 columns per row, from row 1. Filled by draw_screen.
labels  .enc "screen"
        .text " MOUSE LISTENER, CONTROL PORTS 1 AND 2  "
        .text " 1351 POSITION, MICROMYS WHEEL, KEYBOARD"
        .text " X +00000    Y +00000    (POT COUNTS)   "
        .text " STEP X -000 +000  Y -000 +000          "
        .text "                                        "
        .text " BUTTONS     PRESSES                    "
        .text " LEFT        000                        "
        .text " MIDDLE      000                        "
        .text " RIGHT       000                        "
        .text " WHEEL   UP  000   DOWN  000            "
        .text "                                        "
        .text " CURSOR  UP  000   DOWN  000            "
        .text "       LEFT  000   RIGHT 000            "
        .text " KEYS HELD   000                        "
        .text "                                        "
        .text " PORT 2  UP  000   DOWN  000            "
        .text "       LEFT  000   RIGHT 000            "
        .text "       FIRE  000                        "
        .text "                                        "
        .text " POT X/Y $00 $00  LINES $00 $00         "
        .text " FRAMES  00000                          "
        .text "                                        "
        .text " WRITE 1 TO $C001 TO RESET. RESULTS AT  "
        .text " $C000, SEE MOUSE-LISTENER.ASM.         "
        .byte 0
