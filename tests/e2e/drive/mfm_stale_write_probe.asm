; Read sector 1 of MFM cylinder 0, format another cylinder, and read sector 1
; again, all on a 1571 and from a C64.
;
; A C64 cannot take part in a burst transfer, which needs the C128's fast
; serial port. It does not have to: a burst READ with bit 7 of its command byte
; set reads the sector into the drive's buffer at $0300 and leaves out the data
; transfer ('cmdnin' in MFMSUBR2.SRC). Only the status byte still goes out,
; and its handshake ('hskrd' in FASTUTL.SRC) waits for nothing but a change on
; CLK, which the C64 makes through $DD00 bit 4. M-R then fetches the buffer over
; the slow bus. The burst INQUIRE before it reads an ID on cylinder 0 and so
; puts the drive into MFM mode; it ends with the same handshake.
;
; The host supplies DEVICE, CYLINDER and FILL: the cylinder to format on side 0
; and the byte its sectors are filled with.

SETLFS = $ffba
SETNAM = $ffbd
OPEN   = $ffc0
CLOSE  = $ffc3
CHKIN  = $ffc6
CLRCHN = $ffcc
CHRIN  = $ffcf
READST = $ffb7

CIA2_PORT_A = $dd00
CLK_OUT     = $10
JIFFY       = $a2
HANDSHAKE_JIFFIES = 90  ; 1.5 s: the drive spins up, seeks and reads meanwhile

RESULT_STATUS  = $c000
RESULT_READY   = $c001
RESULT_IO      = $c002
RESULT_INQUIRE = $c003
RESULT_READ1   = $c004
RESULT_FORMAT  = $c005
RESULT_READ2   = $c006
RESULT_SECTOR1 = $c100  ; four bytes of sector 1 before the format
RESULT_SECTOR2 = $c104  ; and after it
RESULT_MESSAGE = $c110  ; the drive's answer to the format

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
    sta RESULT_IO
    tsx
    stx stack

    ; On a C64 the 1571 starts as a 1541, which has no MFM; "U0>M1" makes it a
    ; 1571 first.
    ldx #<mode_command
    ldy #>mode_command
    lda #mode_command_end-mode_command
    jsr command

    ; The first INQUIRE after a refused format and a disk change can miss the
    ; ID (21), and the next one finds it. It only reads, so it is tried again.
    lda #3
    sta tries
-   ldx #<inquire_command
    ldy #>inquire_command
    lda #inquire_command_end-inquire_command
    jsr command_with_handshake
    sta RESULT_INQUIRE
    cmp #0
    beq +
    dec tries
    bne -
+

    ldx #0
    jsr read_sector
    sta RESULT_READ1

    ldx #<format_command
    ldy #>format_command
    lda #format_command_end-format_command
    jsr command_with_answer
    sta RESULT_FORMAT
    ldy #0
-   lda answer,y
    sta RESULT_MESSAGE,y
    iny
    cpy #64
    bne -

    ldx #RESULT_SECTOR2-RESULT_SECTOR1
    jsr read_sector
    sta RESULT_READ2

    lda #STATUS_DONE
finish:
    sta RESULT_STATUS
    jsr CLRCHN
    rts

; Wherever a KERNAL call fails, the stack goes back to where 'start' had it.
io_error:
    ldx stack
    txs
    jsr READST
    sta RESULT_IO
    lda #15
    jsr CLOSE
    lda #STATUS_IO_ERROR
    bne finish

; Read sector 1 of cylinder 0, side 0, into the drive's buffer and copy four
; bytes of it to RESULT_SECTOR1 + X. The buffer is cleared first, so a read that
; fails cannot show what an earlier one left there. Returns the drive's answer
; to the read as a BCD byte.
read_sector:
    stx sector_target
    ldx #<clear_command
    ldy #>clear_command
    lda #clear_command_end-clear_command
    jsr command
    ldx #<read_command
    ldy #>read_command
    lda #read_command_end-read_command
    jsr command_with_handshake
    pha
    ldx #<fetch_command
    ldy #>fetch_command
    lda #fetch_command_end-fetch_command
    jsr open_command
    ldx #15
    jsr CHKIN
    bcs io_error
    ldy #0
-   jsr CHRIN
    ldx sector_target
    sta RESULT_SECTOR1,x
    inc sector_target
    iny
    cpy #4
    bne -
    jsr CLRCHN
    lda #15
    jsr CLOSE
    pla
    rts

; Send a command as the name of the command channel and close it again.
command:
    jsr open_command
    lda #15
    jmp CLOSE

; Send a burst command that ends in a handshake, give the handshake, and
; return the drive's answer as a BCD byte.
command_with_handshake:
    jsr open_command
    jsr handshake
    jmp answer_and_close

; Send a command and return the drive's answer as a BCD byte, once it has one.
command_with_answer:
    jsr open_command
answer_and_close:
    jsr read_error_channel
    pha
    lda #15
    jsr CLOSE
    pla
    rts

open_command:
    jsr SETNAM
    lda #15
    ldx #DEVICE
    ldy #15
    jsr SETLFS
    jsr OPEN
    bcc +
    jmp io_error
+   rts

; Pull CLK low, which is the change the drive waits for, hold it until the
; drive is certainly done, and release it again.
handshake:
    lda CIA2_PORT_A
    ora #CLK_OUT
    sta CIA2_PORT_A
    lda JIFFY
    clc
    adc #HANDSHAKE_JIFFIES
-   cmp JIFFY
    bne -
    lda CIA2_PORT_A
    and #$ff-CLK_OUT
    sta CIA2_PORT_A
    rts

; The answer goes to 'answer', up to its carriage return; the two digits of
; the error number are returned in A as one BCD byte.
read_error_channel:
    ldx #15
    jsr CHKIN
    bcc +
    jmp io_error
+   ldy #0
-   jsr CHRIN
    sta answer,y
    iny
    cmp #13
    beq +
    cpy #64
    bne -
+
    jsr CLRCHN
    lda answer
    and #$0f
    asl
    asl
    asl
    asl
    sta bcd
    lda answer+1
    and #$0f
    ora bcd
    rts

; "U0" and the burst INQUIRE DISK command, %00000100, for side 0.
inquire_command:
    .text "U0"
    .byte $04
inquire_command_end:

; "U0" and the burst READ: %10000000 is side 0 with no data transfer, then
; cylinder 0, sector 1, one sector.
read_command:
    .text "U0"
    .byte $80, 0, 1, 1
read_command_end:

; The same partial format the other program sends (mfm_format_command.asm):
; side 0, starting at logical sector 1, interleave 1, 512-byte sectors, nine of
; them, logical and physical cylinder CYLINDER, and the fill byte.
format_command:
    .text "U0"
    .byte $c6, $81, 1, 2, CYLINDER, 9, CYLINDER, CYLINDER, FILL
format_command_end:

clear_command:
    .text "M-W"
    .byte $00, $03, 4, 0, 0, 0, 0
clear_command_end:

fetch_command:
    .text "M-R"
    .byte $00, $03, 4
fetch_command_end:

mode_command:
    .text "U0>M1"
mode_command_end:

stack:
    .byte 0
tries:
    .byte 0
sector_target:
    .byte 0
bcd:
    .byte 0
answer:
    .fill 64, 0
