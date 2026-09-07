; Change VIC color 6 four times per frame through the Ultimate Command
; Interface. The host-side palette E2E test uses this to prove that firmware
; coalesces a burst without disturbing the ordinary video packet sequence.

CTRL    = $DF1C
CMDREG  = $DF1D
STATR   = $DF1F

ST_STATE = $30
ST_LAST  = $20
ST_STAT  = $40

STATUS   = $C000               ; $A5 while running, $5A when complete
COUNT    = $C001               ; completed commands, little endian

FRAMES            = 60
CHANGES_PER_FRAME = 4

        * = $0801

; BASIC line "10 SYS 2061".
        .word basic_end, 10
        .byte $9e
        .text "2061"
        .byte 0
basic_end
        .word 0

start
        sei
        lda #$A5
        sta STATUS
        lda #$00
        sta COUNT
        sta COUNT+1
        lda #FRAMES
        sta frames_left

frame_loop
        jsr wait_frame
        lda #CHANGES_PER_FRAME
        sta changes_left
change_loop
        jsr set_color
        inc red
        lda red
        clc
        adc #12                   ; net +13 including INC
        sta red
        lda green
        clc
        adc #29
        sta green
        lda blue
        clc
        adc #47
        sta blue
        inc COUNT
        bne count_done
        inc COUNT+1
count_done
        dec changes_left
        bne change_loop
        dec frames_left
        bne frame_loop

        lda #$5A
        sta STATUS
        cli
        rts

; Wait for raster line zero in the low half. $D012 also reads zero at line 256,
; so $D011 bit 7 distinguishes the real frame boundary.
wait_frame
wait_away
        bit $D011
        bmi wait_away
        lda $D012
        beq wait_away
wait_zero
        bit $D011
        bmi wait_zero
        lda $D012
        bne wait_zero
        rts

set_color
wait_idle
        lda CTRL
        and #ST_STATE
        bne wait_idle
        lda #$04
        sta CMDREG
        lda #$53
        sta CMDREG
        lda #$06
        sta CMDREG
        lda red
        sta CMDREG
        lda green
        sta CMDREG
        lda blue
        sta CMDREG
        lda #$01
        sta CTRL
wait_reply
        lda CTRL
        and #ST_STATE
        cmp #ST_LAST
        bne wait_reply
drain_status
        lda CTRL
        and #ST_STAT
        beq accept_reply
        lda STATR
        jmp drain_status
accept_reply
        lda #$02
        sta CTRL
        rts

frames_left  .byte 0
changes_left .byte 0
red          .byte 0
green        .byte 85
blue         .byte 170
