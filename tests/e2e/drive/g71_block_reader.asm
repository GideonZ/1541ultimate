; Read one block through the drive's U1 command and hand it to the host.
;
; U1 is used on purpose: it is served by the DOS of a 1541, a 1571 and a 1581
; alike, so this reader needs no ROM entry point and no job queue, and it does
; not change when the drive model does. The host supplies DEVICE, TRACK and
; SECTOR and checks the copied block at RESULT_DATA.

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

    ; command channel
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

    ; buffer channel on "#"
    lda #2
    ldx #DEVICE
    ldy #2
    jsr SETLFS
    lda #1
    ldx #<buffer_name
    ldy #>buffer_name
    jsr SETNAM
    jsr OPEN
    bcs io_error

    jsr init_drive
    jsr send_u1
    jsr read_block
    jsr read_error_channel

    lda #STATUS_DONE
finish:
    sta RESULT_STATUS
    lda #2
    jsr CLOSE
    lda #15
    jsr CLOSE
    jsr CLRCHN
    rts

io_error:
    jsr READST
    sta RESULT_IO
    lda #STATUS_IO_ERROR
    bne finish

; The DOS caches the disk ID and refuses a block whose header carries another
; one. An initialise makes it re-read the BAM, so a freshly mounted image is
; read on its own terms rather than on the previous mount's.
init_drive:
    ldx #15
    jsr CHKOUT
    bcc +
    jmp io_error
+
    ldy #0
-   lda init_command,y
    jsr CHROUT
    iny
    cpy #init_command_end-init_command
    bne -
    lda #13
    jsr CHROUT
    jsr CLRCHN
    rts

send_u1:
    ldx #15
    jsr CHKOUT
    bcc +
    jmp io_error
+
    ldy #0
-   lda u1_command,y
    jsr CHROUT
    iny
    cpy #u1_command_end-u1_command
    bne -
    lda #13
    jsr CHROUT
    jsr CLRCHN
    rts

read_block:
    ldx #2
    jsr CHKIN
    bcc +
    jmp io_error
+
    ldy #0
-   jsr CHRIN
    sta RESULT_DATA,y
    iny
    bne -
    jsr CLRCHN
    rts

; The first byte of the DOS answer is the tens digit of the error number, the
; second the units. Both are kept so that a failure says which error it was
; rather than only that there was one.
read_error_channel:
    ldx #15
    jsr CHKIN
    bcc +
    jmp io_error
+
    jsr CHRIN
    and #$0f
    asl
    asl
    asl
    asl
    sta RESULT_DOS
    jsr CHRIN
    and #$0f
    ora RESULT_DOS
    sta RESULT_DOS
    jsr CLRCHN
    rts

buffer_name:
    .text "#"

init_command:
    .text "i0"
init_command_end:

u1_command:
    .text "u1 2 0 ", format("%d", TRACK), " ", format("%d", SECTOR)
u1_command_end:
