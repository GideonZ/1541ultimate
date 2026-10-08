; Send the 1571's burst format command for MFM tracks and hand the drive's
; answer to the host.
;
; The command goes out as the name of the command channel, so the drive runs it
; as soon as the channel is open; the error channel is read afterwards, which
; waits for the format to finish. Only the command travels over the bus, so the
; C64's slow serial is enough: no burst transfer takes place.
;
; The host supplies DEVICE, FIRST, LAST and FILL: the cylinders to format, both
; on side 0, and the byte the sectors are filled with.

SETLFS = $ffba
SETNAM = $ffbd
OPEN   = $ffc0
CLOSE  = $ffc3
CHKIN  = $ffc6
CLRCHN = $ffcc
CHRIN  = $ffcf
READST = $ffb7

RESULT_STATUS = $c000
RESULT_READY  = $c001
RESULT_DOS    = $c002
RESULT_IO     = $c003
RESULT_DATA   = $c100

STATUS_RUNNING  = $00
STATUS_DONE     = $01
STATUS_IO_ERROR = $02
READY_MARK = $a5

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
    sta RESULT_DOS
    sta RESULT_IO

    ; On a C64 the 1571 starts as a 1541, which has no MFM; "U0>M1" makes it a
    ; 1571 first. The channel is closed again, so the format below opens it anew.
    lda #15
    ldx #DEVICE
    ldy #15
    jsr SETLFS
    lda #mode_command_end-mode_command
    ldx #<mode_command
    ldy #>mode_command
    jsr SETNAM
    jsr OPEN
    bcs io_error
    lda #15
    jsr CLOSE

    lda #15
    ldx #DEVICE
    ldy #15
    jsr SETLFS
    lda #format_command_end-format_command
    ldx #<format_command
    ldy #>format_command
    jsr SETNAM
    jsr OPEN
    bcs io_error

    jsr read_error_channel

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

; The whole answer goes to RESULT_DATA, up to its carriage return, so a failure
; can show the message; the two digits of the error number also go to
; RESULT_DOS as one BCD byte.
read_error_channel:
    ldx #15
    jsr CHKIN
    bcc +
    jmp io_error
+
    ldy #0
-   jsr CHRIN
    sta RESULT_DATA,y
    iny
    cmp #13
    beq +
    cpy #64
    bne -
+
    jsr CLRCHN
    lda RESULT_DATA
    and #$0f
    asl
    asl
    asl
    asl
    sta RESULT_DOS
    lda RESULT_DATA+1
    and #$0f
    ora RESULT_DOS
    sta RESULT_DOS
    rts

; "U0" and the burst command for an MFM format, as the 1571 firmware documents
; it (table at $3811): %11000110 is a partial format with index marks on side 0,
; command 011 for MFM; %10000001 starts at logical sector 1. Then interleave 1,
; sector size code 2 (512 bytes), the last cylinder, 9 sectors per cylinder,
; the logical first cylinder, the physical one it is put on, and the fill byte.
; A partial format is what makes the last cylinder count, so FIRST to LAST and
; nothing else is formatted. Upper case "U0", as the DOS knows its commands
; only in PETSCII.
format_command:
    .text "U0"
    .byte $c6, $81, 1, 2, LAST, 9, FIRST, FIRST, FILL
format_command_end:

mode_command:
    .text "U0>M1"
mode_command_end:
