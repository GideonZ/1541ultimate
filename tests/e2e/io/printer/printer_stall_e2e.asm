; printer_stall_e2e.asm - the C64 side of printer_stall_test.py (issue #1052).
;
; Prints a long Commodore MPS text job to device 4 and, every PROBE_EVERY
; carriage returns, leaves the printer channel and reads the command-channel
; status of the drive on the bus the host names, stamping the CIA 1 time of
; day clock before and after the read. After the printer is closed it does one
; more read. The stamps are what the host turns into "how long did the drive
; make the C64 wait while the printer was busy".
;
; The host writes, before running this PRG:
;   $2000..   the text to print, ended by a $00 byte
;   $CF10     bus id of the drive to probe (8 or 11)
; and reads afterwards:
;   $CF00  done flag: 1 when the job has finished
;   $CF01  number of probes made while printing
;   $CF02  KERNAL status after OPEN + CHKOUT of the printer
;   $CF03  KERNAL status after the last byte was sent
;   $CF07  number of time stamps taken
;   $CA00/$CB00/$CC00  time stamps, one byte per stamp: minutes, seconds,
;                      tenths (BCD, as the CIA keeps them)
;   $CD00  the last status text read
;   $CE00  per probe: carriage returns sent before it
;   $CE80  per probe: length of the status text it read (0 = nothing came back)
; Stamp 0 is the start, probe i takes stamps 1+2i and 2+2i, the stamp after
; CLOSE follows the last in-job probe, and the final probe takes the last two.
;
; Zero page is not used for the program's own state: the existing
; printer_e2e.asm found long CHROUT sessions corrupting $F8-$FA, so counters
; live in plain static storage and the text pointer is patched into code.
;
; Assemble with `make` in this directory (64tass).

PROBE_EVERY = 5
TEXT        = $2000

FLAG        = $cf00
CNT         = $cf01
ST_OPEN     = $cf02
ST_END      = $cf03
CNT2        = $cf07
PARAM_DEV   = $cf10
TM          = $ca00
TS          = $cb00
TT          = $cc00
BUF         = $cd00
PLINE       = $ce00
PLEN        = $ce80

SETLFS = $ffba
SETNAM = $ffbd
OPEN   = $ffc0
CLOSE  = $ffc3
CHKIN  = $ffc6
CHKOUT = $ffc9
CLRCHN = $ffcc
CHRIN  = $ffcf
CHROUT = $ffd2
STATUS = $90

        * = $0801
        .word next, 10
        .byte $9e
        .text "2064"
        .byte 0
next    .word 0

        * = $0810

start:
        lda #0
        sta FLAG
        sta idx
        sta crs
        sta crtot
        sta pcnt
        ; CIA 1 time of day: clock (not alarm) writes, set 01:00:00.0; the
        ; tenths write starts it.
        lda $dc0f
        and #$7f
        sta $dc0f
        lda #1
        sta $dc0b
        lda #0
        sta $dc0a
        sta $dc09
        sta $dc08
        jsr stamp

        lda #4                  ; logical file 4, device 4, secondary 0
        ldx #4
        ldy #0
        jsr SETLFS
        lda #0
        jsr SETNAM
        jsr OPEN
        ldx #4
        jsr CHKOUT
        lda STATUS
        sta ST_OPEN

        lda #<TEXT
        sta fetch+1
        lda #>TEXT
        sta fetch+2
loop:
fetch:  lda TEXT
        beq done
        sta ch
        jsr CHROUT
        lda ch
        cmp #$0d
        bne advance
        inc crtot
        inc crs
        lda crs
        cmp #PROBE_EVERY
        bne advance
        lda #0
        sta crs
        jsr probe
        ldx #4
        jsr CHKOUT
advance: inc fetch+1
        bne loop
        inc fetch+2
        jmp loop

done:
        lda STATUS
        sta ST_END
        jsr CLRCHN
        lda #4
        jsr CLOSE
        jsr stamp
        lda pcnt
        sta CNT
        jsr probe               ; right after the printer CLOSE
        lda idx
        sta CNT2
        lda #1
        sta FLAG
        rts

; OPEN 15,dev,15 ; read the status line to its CR ; CLOSE 15, timed
probe:
        ldx pcnt
        lda crtot
        sta PLINE,x
        jsr CLRCHN
        jsr stamp
        lda #15
        ldx PARAM_DEV
        ldy #15
        jsr SETLFS
        lda #0
        jsr SETNAM
        jsr OPEN
        ldx #15
        jsr CHKIN
        ldy #0
rd:     jsr CHRIN
        sta BUF,y
        cmp #$0d
        beq rdone
        iny
        cpy #40
        bne rd
rdone:  sty slen
        jsr CLRCHN
        lda #15
        jsr CLOSE
        jsr stamp
        ldx pcnt
        lda slen
        sta PLEN,x
        inc pcnt
        rts

stamp:
        ldx idx
        lda $dc0b               ; reading the hours latches the clock
        lda $dc0a
        sta TM,x
        lda $dc09
        sta TS,x
        lda $dc08               ; reading the tenths releases the latch
        sta TT,x
        inc idx
        rts

idx:    .byte 0
crs:    .byte 0
crtot:  .byte 0
pcnt:   .byte 0
ch:     .byte 0
slen:   .byte 0
