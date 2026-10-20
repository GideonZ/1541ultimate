; Read physical track 81, sector 5 through the 1581 job interface. The host
; supplies DEVICE and checks the copied sector at RESULT_DATA.

SETLFS = $ffba
SETNAM = $ffbd
OPEN   = $ffc0
CLOSE  = $ffc3
CHKIN  = $ffc6
CHKOUT = $ffc9
CLRCHN = $ffcc
CHRIN  = $ffcf
CHROUT = $ffd2
READST = $ffb7

RESULT_STATUS = $c000
RESULT_READY  = $c001
RESULT_JOB    = $c002
RESULT_IO     = $c003
RESULT_DATA   = $c100

STATUS_RUNNING = $00
STATUS_DONE    = $01
STATUS_IO_ERROR = $02
READY_MARK = $a5

src = $fb
dst = $fd
index = $02

* = $0801
    .word basic_end, 2026
    .null $9e, format("%d", start)
basic_end:
    .word 0

start:
    lda #STATUS_RUNNING
    sta RESULT_STATUS
    lda #READY_MARK
    sta RESULT_READY
    lda #0
    sta RESULT_JOB
    sta RESULT_IO

    lda #15
    ldx #DEVICE
    ldy #15
    jsr SETLFS
    lda #0
    ldx #0
    ldy #0
    jsr SETNAM
    jsr OPEN
    bcs io_error

    jsr write_drive_code
    jsr execute_drive_code
    jsr read_job_result

    lda #<$0300
    sta src
    lda #>$0300
    sta src+1
    lda #<RESULT_DATA
    sta dst
    lda #>RESULT_DATA
    sta dst+1
    jsr read_128
    jsr read_128
    jsr read_128
    jsr read_128

    lda #STATUS_DONE
finish:
    sta RESULT_STATUS
    lda #15
    jsr CLOSE
    jsr CLRCHN
    rts

io_error:
    jsr READST
    sta RESULT_IO
    lda #STATUS_IO_ERROR
    bne finish

write_drive_code:
    ldx #15
    jsr CHKOUT
    bcc +
    jmp io_error
+
    lda #'M'
    jsr CHROUT
    lda #'-'
    jsr CHROUT
    lda #'W'
    jsr CHROUT
    lda #<drive_code
    jsr CHROUT
    lda #>drive_code
    jsr CHROUT
    lda #drive_code_end-drive_code
    jsr CHROUT
    ldy #0
-   lda drive_code,y
    jsr CHROUT
    iny
    cpy #drive_code_end-drive_code
    bne -
    jsr CLRCHN
    rts

execute_drive_code:
    ldx #15
    jsr CHKOUT
    bcc +
    jmp io_error
+
    lda #'M'
    jsr CHROUT
    lda #'-'
    jsr CHROUT
    lda #'E'
    jsr CHROUT
    lda #<drive_code
    jsr CHROUT
    lda #>drive_code
    jsr CHROUT
    jsr CLRCHN
    rts

read_job_result:
    ldx #15
    jsr CHKOUT
    bcc +
    jmp io_error
+
    lda #'M'
    jsr CHROUT
    lda #'-'
    jsr CHROUT
    lda #'R'
    jsr CHROUT
    lda #<$01ce
    jsr CHROUT
    lda #>$01ce
    jsr CHROUT
    lda #1
    jsr CHROUT
    jsr CLRCHN
    ldx #15
    jsr CHKIN
    bcc +
    jmp io_error
+
    jsr CHRIN
    sta RESULT_JOB
    jsr CLRCHN
    rts

read_128:
    ldx #15
    jsr CHKOUT
    bcc +
    jmp io_error
+
    lda #'M'
    jsr CHROUT
    lda #'-'
    jsr CHROUT
    lda #'R'
    jsr CHROUT
    lda src
    jsr CHROUT
    lda src+1
    jsr CHROUT
    lda #128
    jsr CHROUT
    jsr CLRCHN

    ldx #15
    jsr CHKIN
    bcc +
    jmp io_error
+
    lda #0
    sta index
-   jsr CHRIN
    pha
    ldy index
    pla
    sta (dst),y
    inc index
    bpl -
    jsr CLRCHN

    clc
    lda src
    adc #128
    sta src
    bcc +
    inc src+1
+   clc
    lda dst
    adc #128
    sta dst
    bcc +
    inc dst+1
+   rts

; Wheels MakeSysDisk uses this 1581 ROM job to read physical track 80
; (the 81st track), sector 5 into the drive's $0300-$04ff buffer.
drive_code:
    php
    sei
    lda #$50
    sta $0b
    lda #$05
    sta $0c
    lda #$00
    sta $01ce
    lda #$a4
    ldx #$00
    jsr $ff54
    plp
    rts
drive_code_end:
