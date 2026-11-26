; The C64 side of jiffy_load_soak_test.py (GideonZ/1541ultimate#986).
;
; Loads one file from the Software IEC drive over and over with the KERNAL's own LOAD, to
; the file's own load address, and records for each load what the KERNAL said (carry and
; ST), the first characters of the error channel, the address the load ended at, a 16 bit
; sum and a rotate-and-xor byte of what arrived, and the CIA time of day before and after.
; The load area is filled with $AA before every load, so a load that stops short cannot
; look whole because the previous load left good data behind it.
;
; The host never reads C64 memory while the loop runs, because a memory read halts the
; 6510 and a read that lands in a serial transfer spoils it. It writes the parameters
; below, starts the program, waits, and reads the results when the loop has ended.
;
; Parameters, written before the program starts:
;   $C000 loads to run (1..100)      $C001 device number        $C002 delay passes (0 = none)
;   $C003 pages in the file          $C004 name length, $C005 name (16 bytes at most)
;   $C015 length of the CP command, $C016 the command (8 bytes at most)
;   $C020 length of the CD command, $C021 the command (79 bytes at most)
; Results, read afterwards:
;   $C080 $4A once started   $C081 $FF when the loop has ended   $C082 loads finished
;   $C083 failure texts stored   $C090 / $C0B0 / $C0D0 the CP, CD and final status lines
;   $C100 the error channel text of the first eight loads that were not clean, 32 bytes each
;   $C200 one 16 byte record per load: carry, ST, sum lo, sum hi, xor, end lo, end hi, the
;         first two error characters, tenths/seconds/minutes before, and the same after
;
; The delay routine is the host delay of the test's patched KERNAL: its LOAD calls $0340 in
; place of one ORA/TAX pair, and the routine waits before doing them. See jiffy_load.py.

SETLFS = $ffba
SETNAM = $ffbd
OPEN = $ffc0
CLOSE = $ffc3
CHKIN = $ffc6
CLRCHN = $ffcc
CHRIN = $ffcf
LOAD = $ffd5
ST = $90

LOADAT = $2000
P_COUNT = $c000
P_DEV = $c001
P_DELAY = $c002
P_PAGES = $c003
P_NAMELEN = $c004
P_NAME = $c005
P_CPLEN = $c015
P_CP = $c016
P_CDLEN = $c020
P_CD = $c021
RES = $c080
FAILS = $c100
RECORDS = $c200
ERRBUF = $c900
DELAY = $0340
ZP = $fb            ; word: the load area, and the file in the checksum
ZP2 = $fd           ; word: a record, a failure slot, a status destination

        * = $0801
        .word (+), 10
        .byte $9e
        .text "2061"
        .byte 0
+       .word 0

        * = $080d
start   sei
        cld
        ; clear the header, then the failure texts, the records and the error buffer
        lda #0
        ldx #$7f
-       sta RES,x
        dex
        bpl -
        sta ZP
        lda #>FAILS
        sta ZP+1
        ldx #8              ; the pages from FAILS to the record area's end
        ldy #0
-       lda #0
        sta (ZP),y
        iny
        bne -
        inc ZP+1
        dex
        bne -
        ; install the delay routine when the host asked for one
        lda P_DELAY
        beq +
        ldx #8
-       lda delayrt,x
        sta DELAY,x
        dex
        bpl -
        lda P_DELAY
        sta DELAY+1
+       ; start the time of day clock at 1:00:00.0
        lda #1
        sta $dc0b
        lda #0
        sta $dc0a
        sta $dc09
        sta $dc08
        cli
        lda #$4a
        sta RES
        ; select the partition, then the directory, and keep both status lines
        lda P_CPLEN
        ldx #<P_CP
        ldy #>P_CP
        jsr command
        lda #<(RES+$10)
        ldy #>(RES+$10)
        jsr copyerr
        lda P_CDLEN
        ldx #<P_CD
        ldy #>P_CD
        jsr command
        lda #<(RES+$30)
        ldy #>(RES+$30)
        jsr copyerr
        lda #0
        sta RES+2
mainlp  jsr fill
        ldx #0
        jsr readtod
        lda #1
        ldx P_DEV
        ldy #1
        jsr SETLFS
        lda P_NAMELEN
        ldx #<P_NAME
        ldy #>P_NAME
        jsr SETNAM
        lda #0
        sta ST
        lda #0
        jsr LOAD
        stx endlo
        sty endhi
        lda #0
        rol
        sta tcarry
        lda ST
        sta tst
        ldx #3
        jsr readtod
        jsr geterr
        jsr checksum
        jsr record
        inc RES+2
        lda RES+2
        cmp P_COUNT
        bcc mainlp
        jsr geterr
        lda #<(RES+$50)
        ldy #>(RES+$50)
        jsr copyerr
        lda #$ff
        sta RES+1
        lda #5
        sta $d020
halt    jmp halt

; command: OPEN 15 on the device with the name at X/Y, length in A, and read the status
command jsr SETNAM
        lda #15
        ldx P_DEV
        ldy #15
        jsr SETLFS
        jsr OPEN
        jmp readerr

; geterr: the error channel, opened and read and closed again, into ERRBUF
geterr  lda #15
        ldx P_DEV
        ldy #15
        jsr SETLFS
        lda #0
        jsr SETNAM
        jsr OPEN
readerr ldx #31
        lda #0
-       sta ERRBUF,x
        dex
        bpl -
        ldx #15
        jsr CHKIN
        ldy #0
-       jsr CHRIN
        sta ERRBUF,y
        cmp #13
        beq +
        iny
        cpy #31
        bcc -
+       jsr CLRCHN
        lda #15
        jmp CLOSE

; copyerr: ERRBUF to the 32 bytes at A/Y
copyerr sta ZP2
        sty ZP2+1
        ldy #31
-       lda ERRBUF,y
        sta (ZP2),y
        dey
        bpl -
        rts

fill    lda #0
        sta ZP
        lda #>LOADAT
        sta ZP+1
        ldx P_PAGES
        ldy #0
        lda #$aa
-       sta (ZP),y
        iny
        bne -
        inc ZP+1
        dex
        bne -
        rts

; readtod: tenths, seconds and minutes into todbuf+X; reading hours latches the clock
readtod lda $dc0b
        lda $dc0a
        sta todbuf+2,x
        lda $dc09
        sta todbuf+1,x
        lda $dc08
        sta todbuf,x
        rts

checksum
        lda #0
        sta s16
        sta s16+1
        sta rx
        sta ZP
        lda #>LOADAT
        sta ZP+1
        ldx P_PAGES
        ldy #0
-       lda (ZP),y
        pha
        clc
        adc s16
        sta s16
        bcc +
        inc s16+1
+       lda rx
        asl
        adc #0
        sta rx
        pla
        eor rx
        sta rx
        iny
        bne -
        inc ZP+1
        dex
        bne -
        rts

; record: the 16 byte record of this load, and its error text when it was not clean
record  lda RES+2
        sta ZP2
        lda #0
        sta ZP2+1
        ldx #4
-       asl ZP2
        rol ZP2+1
        dex
        bne -
        lda ZP2+1
        clc
        adc #>RECORDS
        sta ZP2+1
        ldy #0
        lda tcarry
        sta (ZP2),y
        iny
        lda tst
        sta (ZP2),y
        iny
        lda s16
        sta (ZP2),y
        iny
        lda s16+1
        sta (ZP2),y
        iny
        lda rx
        sta (ZP2),y
        iny
        lda endlo
        sta (ZP2),y
        iny
        lda endhi
        sta (ZP2),y
        iny
        lda ERRBUF
        sta (ZP2),y
        iny
        lda ERRBUF+1
        sta (ZP2),y
        iny
        ldx #0
-       lda todbuf,x
        sta (ZP2),y
        iny
        inx
        cpx #6
        bcc -
        ; was it clean? carry, ST at end of file, the whole length and "00" say so
        lda tcarry
        bne bad
        lda tst
        cmp #$40
        bne bad
        lda endlo
        bne bad
        lda P_PAGES
        clc
        adc #>LOADAT
        cmp endhi
        bne bad
        lda ERRBUF
        cmp #$30
        bne bad
        lda ERRBUF+1
        cmp #$30
        bne bad
        rts
bad     lda RES+3
        cmp #8
        bcs +
        asl
        asl
        asl
        asl
        asl
        sta ZP2
        lda #>FAILS
        sta ZP2+1
        lda ZP2
        ldy ZP2+1
        jsr copyerr
        inc RES+3
+       rts

delayrt .byte $a2, 0, $ca, $d0, $fd, $09, $20, $aa, $60
s16     .word 0
rx      .byte 0
tcarry  .byte 0
tst     .byte 0
endlo   .byte 0
endhi   .byte 0
todbuf  .fill 6
