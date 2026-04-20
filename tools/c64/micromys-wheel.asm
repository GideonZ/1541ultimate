; ------------------------------------------------------------
; Micromys wheel pulse conformance measurement tool for C64
; ------------------------------------------------------------
;
; Measures the active-low duration of Micromys wheel pulses on
; joystick port 1 and validates them against the Micromys spec:
;
;   50,000 us +/- 4%
;
; CIA1 port B ($DC01), active low:
;   bit 2 = wheel up
;   bit 3 = wheel down
;
; Output example:
;   PAL U CYC=50126 US=50877 ERR=+00877 OK
;
; Notes:
; - This is a conformance probe for firmware behavior
; - Validation is time-based, not cycle-based
; - PAL / NTSC is detected once at startup
; - One full 1->0->1 low pulse is treated as one event
; - After each event, the code waits for a stable idle gap
;   before re-arming, to avoid double-logging
;
; ------------------------------------------------------------

        * = $0801
        .word basic_next
        .word 10
        .byte $9e
        .text "2064"
        .byte 0
basic_next:
        .word 0

        * = $0810

; ------------------------------------------------------------
; Constants
; ------------------------------------------------------------

CHROUT      = $ffd2

CPU_PORT    = $01

CIA1_PRB    = $dc01
CIA1_TALO   = $dc04
CIA1_TAHI   = $dc05
CIA1_TBLO   = $dc06
CIA1_TBHI   = $dc07
CIA1_ICR    = $dc0d
CIA1_CRA    = $dc0e
CIA1_CRB    = $dc0f

VIC_RASTER  = $d012
VIC_CTRL1   = $d011

MASK_UP     = %00000100
MASK_DN     = %00001000
MASK_WHEEL  = MASK_UP | MASK_DN

; Nominal low pulse length for 50,000 us
TARGET_PAL_LO   = <49262
TARGET_PAL_HI   = >49262

TARGET_NTSC_LO  = <51136
TARGET_NTSC_HI  = >51136

; 50,000 us +/- 2,000 us
ERR_LIMIT_LO    = <2000
ERR_LIMIT_HI    = >2000

; Stable high gap before re-arm.
; Roughly 20 ms on PAL. Good enough as a practical debounce.
REARM_LO    = <19705
REARM_HI    = >19705

IDEAL_US_LO = <50000
IDEAL_US_HI = >50000

; ------------------------------------------------------------
; Entry
; ------------------------------------------------------------

start:
        sei
        lda #$37
        sta CPU_PORT

        lda #$7f
        sta CIA1_ICR
        lda CIA1_ICR

        jsr detect_video_standard
        jsr init_mode_constants

main_loop:
        jsr wait_for_idle
        jsr detect_pulse_start
        jsr measure_active_low_pulse
        jsr compute_metrics
        jsr print_result_line
        jsr wait_for_stable_idle_gap
        jmp main_loop

; ------------------------------------------------------------
; Mode initialization
; ------------------------------------------------------------

init_mode_constants:
        lda is_pal
        beq init_mode_constants_ntsc

        lda #TARGET_PAL_LO
        sta target_lo
        lda #TARGET_PAL_HI
        sta target_hi
        rts

init_mode_constants_ntsc:
        lda #TARGET_NTSC_LO
        sta target_lo
        lda #TARGET_NTSC_HI
        sta target_hi
        rts

; ------------------------------------------------------------
; State machine
; ------------------------------------------------------------

wait_for_idle:
wait_for_idle_loop:
        lda CIA1_PRB
        and #MASK_WHEEL
        cmp #MASK_WHEEL
        bne wait_for_idle_loop
        rts

detect_pulse_start:
detect_pulse_start_wait_fall:
        lda CIA1_PRB
        and #MASK_WHEEL
        cmp #MASK_WHEEL
        beq detect_pulse_start_wait_fall

        sta pulse_state

        lda pulse_state
        and #MASK_UP
        beq detect_pulse_start_got_up

        lda pulse_state
        and #MASK_DN
        beq detect_pulse_start_got_down

        jmp detect_pulse_start

detect_pulse_start_got_up:
        lda #'U'
        sta pulse_dir
        lda #MASK_UP
        sta pulse_mask
        rts

detect_pulse_start_got_down:
        lda #'D'
        sta pulse_dir
        lda #MASK_DN
        sta pulse_mask
        rts

measure_active_low_pulse:
        lda #0
        sta pulse_overflow

        lda CIA1_ICR

        lda #$ff
        sta CIA1_TALO
        sta CIA1_TAHI

        ; Timer A: one-shot + force load + start, phi2
        lda #%00011001
        sta CIA1_CRA

measure_active_low_pulse_wait_release:
        lda CIA1_PRB
        and pulse_mask
        bne measure_active_low_pulse_released

        lda CIA1_ICR
        and #%00000001
        beq measure_active_low_pulse_wait_release

measure_active_low_pulse_overflow:
        lda #$00
        sta CIA1_CRA

        lda #1
        sta pulse_overflow

        lda #$ff
        sta cycles_lo
        sta cycles_hi
        rts

measure_active_low_pulse_released:

        lda #$00
        sta CIA1_CRA

        lda CIA1_ICR

        lda CIA1_TALO
        sta timer_lo
        lda CIA1_TAHI
        sta timer_hi

        lda #$ff
        sec
        sbc timer_lo
        sta cycles_lo

        lda #$ff
        sbc timer_hi
        sta cycles_hi

        rts

wait_for_stable_idle_gap:
wait_for_stable_idle_gap_wait_high:
        lda CIA1_PRB
        and #MASK_WHEEL
        cmp #MASK_WHEEL
        bne wait_for_stable_idle_gap_wait_high

        lda #$00
        sta CIA1_CRB

        lda CIA1_ICR

        lda #REARM_LO
        sta CIA1_TBLO
        lda #REARM_HI
        sta CIA1_TBHI

        ; Timer B: one-shot + force load + start, phi2
        lda #%00011001
        sta CIA1_CRB

wait_for_stable_idle_gap_check:
        lda CIA1_PRB
        and #MASK_WHEEL
        cmp #MASK_WHEEL
        bne wait_for_stable_idle_gap_restart

        lda CIA1_ICR
        and #%00000010
        beq wait_for_stable_idle_gap_check

        lda #$00
        sta CIA1_CRB
        rts

wait_for_stable_idle_gap_restart:
        lda #$00
        sta CIA1_CRB
        jmp wait_for_stable_idle_gap_wait_high

; ------------------------------------------------------------
; Metric computation
; ------------------------------------------------------------

compute_metrics:
        ; signed cycle error = measured_cycles - target_cycles
        lda cycles_lo
        sec
        sbc target_lo
        sta error_cycles_lo

        lda cycles_hi
        sbc target_hi
        sta error_cycles_hi

        lda #0
        sta error_negative

        lda error_cycles_hi
        bmi compute_metrics_negative

compute_metrics_positive:
        lda error_cycles_lo
        sta abs_cycles_lo
        lda error_cycles_hi
        sta abs_cycles_hi
        jmp compute_metrics_scale

compute_metrics_negative:
        lda #1
        sta error_negative

        lda error_cycles_lo
        eor #$ff
        clc
        adc #1
        sta abs_cycles_lo

        lda error_cycles_hi
        eor #$ff
        adc #0
        sta abs_cycles_hi

compute_metrics_scale:
        jsr scale_abs_cycles_to_abs_us
        jsr build_measured_us
        rts

; ------------------------------------------------------------
; Convert absolute cycle error to absolute microsecond error
;
; Input:
;   abs_cycles_hi:abs_cycles_lo
;   is_pal
;
; Output:
;   abs_error_us_hi:abs_error_us_lo
;
; PAL:  us ~= cycles * 203 / 200
; NTSC: us ~= cycles *  44 /  45
;
; Rounded to nearest integer by adding divisor/2 before division.
; ------------------------------------------------------------

scale_abs_cycles_to_abs_us:
        lda #0
        sta prod0
        sta prod1
        sta prod2

        lda abs_cycles_lo
        sta mcand0
        lda abs_cycles_hi
        sta mcand1
        lda #0
        sta mcand2

        lda is_pal
        beq scale_abs_cycles_to_abs_us_ntsc

scale_abs_cycles_to_abs_us_pal:
        lda #203
        sta multiplier
        lda #200
        sta divisor
        lda #100
        sta round_add
        jmp scale_abs_cycles_to_abs_us_mul

scale_abs_cycles_to_abs_us_ntsc:
        lda #44
        sta multiplier
        lda #45
        sta divisor
        lda #22
        sta round_add

scale_abs_cycles_to_abs_us_mul:
scale_mul_loop:
        lda multiplier
        beq scale_mul_done

        lsr multiplier
        bcc scale_mul_noadd

        clc
        lda prod0
        adc mcand0
        sta prod0

        lda prod1
        adc mcand1
        sta prod1

        lda prod2
        adc mcand2
        sta prod2

scale_mul_noadd:
        asl mcand0
        rol mcand1
        rol mcand2
        jmp scale_mul_loop

scale_mul_done:
        clc
        lda prod0
        adc round_add
        sta prod0

        lda prod1
        adc #0
        sta prod1

        lda prod2
        adc #0
        sta prod2

        lda #0
        sta quotient_lo
        sta quotient_hi

scale_div_loop:
        lda prod2
        bne scale_div_subtract

        lda prod1
        bne scale_div_subtract

        lda prod0
        cmp divisor
        bcc scale_div_done

scale_div_subtract:
        sec
        lda prod0
        sbc divisor
        sta prod0

        lda prod1
        sbc #0
        sta prod1

        lda prod2
        sbc #0
        sta prod2

        inc quotient_lo
        bne scale_div_loop
        inc quotient_hi
        jmp scale_div_loop

scale_div_done:
        lda quotient_lo
        sta abs_error_us_lo
        lda quotient_hi
        sta abs_error_us_hi
        rts

; ------------------------------------------------------------
; measured_us = 50000 +/- abs_error_us
; ------------------------------------------------------------

build_measured_us:
        lda error_negative
        bne build_measured_us_negative

build_measured_us_positive:
        clc
        lda #IDEAL_US_LO
        adc abs_error_us_lo
        sta measured_us_lo

        lda #IDEAL_US_HI
        adc abs_error_us_hi
        sta measured_us_hi
        rts

build_measured_us_negative:
        lda #IDEAL_US_LO
        sec
        sbc abs_error_us_lo
        sta measured_us_lo

        lda #IDEAL_US_HI
        sbc abs_error_us_hi
        sta measured_us_hi
        rts

; ------------------------------------------------------------
; Printing
; ------------------------------------------------------------

print_result_line:
        jsr print_newline
        jsr print_video_prefix
        jsr print_direction
        jsr print_cycles_field
        jsr print_us_field
        jsr print_error_field
        jsr print_verdict
        rts

print_video_prefix:
        lda is_pal
        beq print_video_prefix_ntsc

        ldx #0
print_video_prefix_pal_loop:
        lda txt_pal,x
        beq print_video_prefix_done
        jsr CHROUT
        inx
        bne print_video_prefix_pal_loop

print_video_prefix_done:
        rts

print_video_prefix_ntsc:
        ldx #0
print_video_prefix_ntsc_loop:
        lda txt_ntsc,x
        beq print_video_prefix_done
        jsr CHROUT
        inx
        bne print_video_prefix_ntsc_loop

print_direction:
        lda pulse_dir
        jsr CHROUT
        lda #' '
        jsr CHROUT
        rts

print_cycles_field:
        ldx #0
print_cycles_field_label_loop:
        lda txt_cyc,x
        beq print_cycles_field_label_done
        jsr CHROUT
        inx
        bne print_cycles_field_label_loop

print_cycles_field_label_done:
        lda cycles_lo
        sta print_lo
        lda cycles_hi
        sta print_hi
        jsr print_u16_5d
        rts

print_us_field:
        ldx #0
print_us_field_label_loop:
        lda txt_us,x
        beq print_us_field_label_done
        jsr CHROUT
        inx
        bne print_us_field_label_loop

print_us_field_label_done:
        lda measured_us_lo
        sta print_lo
        lda measured_us_hi
        sta print_hi
        jsr print_u16_5d
        rts

print_error_field:
        ldx #0
print_error_field_label_loop:
        lda txt_err,x
        beq print_error_field_label_done
        jsr CHROUT
        inx
        bne print_error_field_label_loop

print_error_field_label_done:
        lda error_negative
        bne print_error_field_negative

print_error_field_positive:
        lda #'+'
        jsr CHROUT
        lda abs_error_us_lo
        sta print_lo
        lda abs_error_us_hi
        sta print_hi
        jsr print_u16_5d
        rts

print_error_field_negative:
        lda #'-'
        jsr CHROUT
        lda abs_error_us_lo
        sta print_lo
        lda abs_error_us_hi
        sta print_hi
        jsr print_u16_5d
        rts

print_verdict:
        lda #' '
        jsr CHROUT

        lda pulse_overflow
        bne print_verdict_fail

        lda abs_error_us_hi
        cmp #ERR_LIMIT_HI
        bcc print_verdict_ok
        bne print_verdict_fail

        lda abs_error_us_lo
        cmp #ERR_LIMIT_LO
        bcc print_verdict_ok
        beq print_verdict_ok

print_verdict_fail:
        ldx #0
print_verdict_fail_loop:
        lda txt_fail,x
        beq print_verdict_done
        jsr CHROUT
        inx
        bne print_verdict_fail_loop

print_verdict_done:
        rts

print_verdict_ok:
        ldx #0
print_verdict_ok_loop:
        lda txt_ok,x
        beq print_verdict_done
        jsr CHROUT
        inx
        bne print_verdict_ok_loop

print_newline:
        lda #13
        jsr CHROUT
        rts

; ------------------------------------------------------------
; Fixed-width unsigned 16-bit decimal printer
; Input: print_hi:print_lo
; Output: always 5 digits, zero-padded
; ------------------------------------------------------------

print_u16_5d:
        lda #0
        sta digit_value

; 10000 = $2710
print_u16_5d_10000_loop:
        lda print_hi
        cmp #$27
        bcc print_u16_5d_1000_emit
        bne print_u16_5d_10000_sub
        lda print_lo
        cmp #$10
        bcc print_u16_5d_1000_emit

print_u16_5d_10000_sub:
        sec
        lda print_lo
        sbc #$10
        sta print_lo
        lda print_hi
        sbc #$27
        sta print_hi
        inc digit_value
        jmp print_u16_5d_10000_loop

print_u16_5d_1000_emit:
        lda digit_value
        ora #'0'
        jsr CHROUT
        lda #0
        sta digit_value

; 1000 = $03E8
print_u16_5d_1000_loop:
        lda print_hi
        cmp #$03
        bcc print_u16_5d_100_emit
        bne print_u16_5d_1000_sub
        lda print_lo
        cmp #$e8
        bcc print_u16_5d_100_emit

print_u16_5d_1000_sub:
        sec
        lda print_lo
        sbc #$e8
        sta print_lo
        lda print_hi
        sbc #$03
        sta print_hi
        inc digit_value
        jmp print_u16_5d_1000_loop

print_u16_5d_100_emit:
        lda digit_value
        ora #'0'
        jsr CHROUT
        lda #0
        sta digit_value

; 100 = $0064
print_u16_5d_100_loop:
        lda print_hi
        bne print_u16_5d_100_sub
        lda print_lo
        cmp #100
        bcc print_u16_5d_10_emit

print_u16_5d_100_sub:
        sec
        lda print_lo
        sbc #100
        sta print_lo
        lda print_hi
        sbc #0
        sta print_hi
        inc digit_value
        jmp print_u16_5d_100_loop

print_u16_5d_10_emit:
        lda digit_value
        ora #'0'
        jsr CHROUT
        lda #0
        sta digit_value

; 10 = $000A
print_u16_5d_10_loop:
        lda print_hi
        bne print_u16_5d_10_sub
        lda print_lo
        cmp #10
        bcc print_u16_5d_1_emit

print_u16_5d_10_sub:
        sec
        lda print_lo
        sbc #10
        sta print_lo
        lda print_hi
        sbc #0
        sta print_hi
        inc digit_value
        jmp print_u16_5d_10_loop

print_u16_5d_1_emit:
        lda digit_value
        ora #'0'
        jsr CHROUT

        lda print_lo
        ora #'0'
        jsr CHROUT
        rts

; ------------------------------------------------------------
; PAL / NTSC detection
; Runs once at startup.
; ------------------------------------------------------------

detect_video_standard:
detect_video_standard_wait_raster_zero:
        lda VIC_RASTER
        bne detect_video_standard_wait_raster_zero

detect_video_standard_wait_raster_nonzero:
        lda VIC_RASTER
        beq detect_video_standard_wait_raster_nonzero

detect_video_standard_wait_high_range:
        lda VIC_CTRL1
        bpl detect_video_standard_wait_high_range

detect_video_standard_check:
        lda VIC_RASTER
        cmp #$20
        bcs detect_video_standard_set_pal

        lda VIC_CTRL1
        bmi detect_video_standard_check

        lda #0
        sta is_pal
        rts

detect_video_standard_set_pal:
        lda #1
        sta is_pal
        rts

; ------------------------------------------------------------
; Text
; ------------------------------------------------------------

txt_pal:
        .text "PAL "
        .byte 0

txt_ntsc:
        .text "NTSC "
        .byte 0

txt_cyc:
        .text "CYC="
        .byte 0

txt_us:
        .text " US="
        .byte 0

txt_err:
        .text " ERR="
        .byte 0

txt_ok:
        .text "OK"
        .byte 0

txt_fail:
        .text "FAIL"
        .byte 0

; ------------------------------------------------------------
; Variables
; ------------------------------------------------------------

pulse_state:
        .byte 0

pulse_mask:
        .byte 0

pulse_dir:
        .byte 0

pulse_overflow:
        .byte 0

is_pal:
        .byte 0

target_lo:
        .byte 0
target_hi:
        .byte 0

timer_lo:
        .byte 0
timer_hi:
        .byte 0

cycles_lo:
        .byte 0
cycles_hi:
        .byte 0

error_cycles_lo:
        .byte 0
error_cycles_hi:
        .byte 0

error_negative:
        .byte 0

abs_cycles_lo:
        .byte 0
abs_cycles_hi:
        .byte 0

abs_error_us_lo:
        .byte 0
abs_error_us_hi:
        .byte 0

measured_us_lo:
        .byte 0
measured_us_hi:
        .byte 0

mcand0:
        .byte 0
mcand1:
        .byte 0
mcand2:
        .byte 0

prod0:
        .byte 0
prod1:
        .byte 0
prod2:
        .byte 0

multiplier:
        .byte 0

divisor:
        .byte 0

round_add:
        .byte 0

quotient_lo:
        .byte 0
quotient_hi:
        .byte 0

print_lo:
        .byte 0
print_hi:
        .byte 0

digit_value:
        .byte 0