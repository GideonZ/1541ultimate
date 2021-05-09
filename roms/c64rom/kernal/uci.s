;----------------------------------------------------
; Routines for loading / saving / fileio through UCI
;
; Programmed by Gideon Zweijtzer
;
; Copyright (c) 2020 - Gideon Zweijtzer
;
;----------------------------------------------------
        .segment "ULTIMATE"
;
        CMD_IF_CONTROL = $DF1C
        CMD_IF_COMMAND = $DF1D
        CMD_IF_RESULT  = $DF1E
        CMD_IF_STATUS  = $DF1F

        UCI_IDENTIFIER = $C9
        CMD_PUSH_CMD   = $01
        CMD_NEXT_DATA  = $02
        CMD_ABORT      = $04
        CMD_ERROR      = $08

        CMD_STATE_BITS      = $30
        CMD_STATE_DATA      = $20
        CMD_STATE_IDLE      = $00
        CMD_STATE_BUSY      = $10
        CMD_STATE_LAST_DATA = $20
        CMD_STATE_MORE_DATA = $30

        UCI_TARGET     = $05
        UCI_CMD_LOADSU = $10
        UCI_CMD_LOADEX = $11
        UCI_CMD_SAVE   = $12
        UCI_CMD_OPEN   = $13
        UCI_CMD_CLOSE  = $14
        UCI_CMD_CHKIN  = $15
        UCI_CMD_CHKOUT = $16

        STATUS     = $90
        VERIFYFLAG = $93
        LOADPNTR   = $AE
        SECADDR    = $B9
        DEVNUM     = $BA
        NAMEPTR    = $BB
        NAMELEN    = $B7
        LOADADDR   = $C3
        SAVEADDR   = $C1
        SAVEEND    = $AE

        MY_OUTLEN       = $02BF ; Last byte of free area, before sprite 11
        UCI_PENDING_CMD = $02BE
        UCI_LAST_CMD    = $02BD
        UCI_LAST_SA     = $02BC


ulti_restor
            lda CMD_IF_COMMAND
            cmp #UCI_IDENTIFIER
            bne rest1
            jsr uci_abort
            jsr uci_clear_error
rest1       jmp restor

; $FFD5 
; LOAD. Load or verify file. (Must call SETLFS and SETNAM beforehand.)
; Input: A: 0 = Load, 1-255 = Verify; X/Y = Load address (if secondary address = 0).
; Output: Carry: 0 = No errors, 1 = Error; A = KERNAL error code (if Carry = 1); X/Y = Address of last byte loaded/verified (if Carry = 0).
; Used registers: A, X, Y.
; Real address: $F49E.
; Vectors through $0330, after storing X in $C3 and Y in $C4 (LOADADDR)


ultiload    ldx DEVNUM
            cpx OUR_DEVICE
            beq myload
ld1         jmp (iload)

myload      sta VERIFYFLAG
            lda #$00
            sta STATUS
            ldy NAMELEN
            bne ld2
            jmp error8

ld2         lda CMD_IF_COMMAND
            cmp #UCI_IDENTIFIER
            beq ld3
            lda VERIFYFLAG
            jmp (iload)

ld3         ldx SECADDR
            jsr luking ; print SEARCHING
            ldx #UCI_CMD_LOADSU
            jsr uci_setup_cmd
            ldy #LOADADDR
            jsr uci_setup_range
            jsr uci_filename ; also executes
            lda CMD_IF_STATUS
            jsr uci_ack ; restores A
            beq ld4 ; all OK when zero
            jmp error4

ld4         jsr loding ; print LOADING

            ldx #UCI_CMD_LOADEX
            jsr uci_setup_cmd
            jsr uci_execute
            lda CMD_IF_STATUS
            bmi _verify_err

            lda CMD_IF_STATUS
            sta LOADPNTR
            lda CMD_IF_STATUS
            sta LOADPNTR+1
            ;jsr show_end_addr

            jsr uci_ack ; restores A
            ldx LOADPNTR
            ldy LOADPNTR+1
            clc
            rts

_verify_err jsr uci_ack ; restores A
            lda #$10
            ora STATUS
            sta STATUS
            clc
            rts

; $FFD8   
; SAVE. Save file. (Must call SETLFS and SETNAM beforehand.)
; Input: A = Address of zero page register holding start address of memory area to save; X/Y = End address of memory area plus 1.
; Output: Carry: 0 = No errors, 1 = Error; A = KERNAL error code (if Carry = 1).
; Used registers: A, X, Y.
; Real address: $F5DD.
; Vector through $0332, after storing start address in $C1 and $C2 and end address in $AE and $AF

ultisave    lda DEVNUM
            cmp OUR_DEVICE
            beq mysave
sv1         jmp (isave)

mysave
            lda #$00
            sta STATUS
            ldy NAMELEN
            bne sv2
            jmp error8

sv2         lda CMD_IF_COMMAND
            cmp #UCI_IDENTIFIER
            bne sv1

            jsr saving ; print SAVING

            ldx #UCI_CMD_SAVE
            jsr uci_setup_cmd
            ldy #SAVEADDR
            jsr uci_setup_range
            jsr uci_filename
            lda CMD_IF_STATUS
            beq sv3 ; all OK when zero

            jsr uci_ack
            sec
            rts

sv3         jsr uci_ack
            clc
            rts

; $FFC0   
; OPEN. Open file. (Must call SETLFS and SETNAM beforehand.)
; Input: –
; Output: –
; Used registers: A, X, Y.
; Real address: ($031A), $F34A.
; 

ultiopen    cmp OUR_DEVICE
            beq myopen
op3         cmp #3
            beq op2       ;is screen...done.
op1         rts ; return to original open routine with original flags

myopen      ldx CMD_IF_COMMAND
            cpx #UCI_IDENTIFIER
            bne op3

            ldx #UCI_CMD_OPEN
            jsr uci_setup_cmd
            jsr uci_filename ; also executes
            jsr uci_ack

op2         pla ; throw away return address and exit
            pla
            clc
            rts

; $FFC3   
; CLOSE. Close file.
; Input: A = Logical number.
; Output: –
; Used registers: A, X, Y.
; Real address: ($031C), $F291.
;

ulticlose   cmp OUR_DEVICE
            beq myclose
cl1         cmp #3
            beq closeexit ;is screen...done
            rts           ; return to caller with original flags due to cmp #3

myclose     ldx CMD_IF_COMMAND
            cpx #UCI_IDENTIFIER
            bne cl1

            ldx #UCI_CMD_CLOSE
            jsr uci_setup_cmd
            jsr uci_execute
            jsr uci_ack
closeexit   pla         ; kill return address
            pla         ; kill return address			
            jmp jx150

; $FFE4   
; GETIN. Read byte from default input. (If not keyboard, must call OPEN and CHKIN beforehand.)
; Input: –
; Output: A = Byte read.
; Used registers: A, X, Y.
; Real address: ($032A), $F13E.


;getinentry
;            lda DEVFROM
;            cmp OUR_DEVICE
;            beq my_chrin
;            jmp (origvect_getin)


; $FFCF   
; CHRIN. Read byte from default input (for keyboard, read a line from the screen). (If not keyboard, must call OPEN and CHKIN beforehand.)
; Input: –
; Output: A = Byte read.
; Used registers: A, Y. (For serial connections, Y is not modified, so we should not modify it!)
; Real address: ($0324), $F157.
 

ultichrin   cmp OUR_DEVICE
            beq my_chrin
            cmp #3          ;is input from screen?
            bne cin1        ;no...
            jmp bn15
cin1        jmp bn20

my_chrin
; is there any data available in the current buffer?
            lda CMD_IF_CONTROL
            bpl _no_data_avail

            ; read available data and store it on the stack
            lda CMD_IF_RESULT
            pha

            ; a byte was succesfully read. However, was this the last byte?
            lda CMD_IF_CONTROL
            bmi _ok   ; there is more in the current buffer

            ; end of current buffer. Is this the last buffer?
            and #CMD_STATE_BITS
            cmp #CMD_STATE_MORE_DATA
            beq _ok   ; there is a next buffer. So we are fine

            ; No next buffer available, so set EOI
            lda #$40
            ora STATUS
            sta STATUS

            ; pick up the byte we read and leave
_ok         pla
            clc
            rts ; done!!


_no_data_avail
            ; Current buffer is empty. Are we in the MORE data state?
            and #CMD_STATE_BITS
            cmp #CMD_STATE_MORE_DATA
            bne _read_error

            ; Get next block of data
            jsr uci_ack
            jsr uci_wait_busy
            jmp my_chrin

_read_error
            ; No data could be read. We return $0D, and set EOF + Read Error
            lda #$42
            ora STATUS
            sta STATUS
            lda #$0D
            clc
            rts


; $FFD2   
; CHROUT. Write byte to default output. (If not screen, must call OPEN and CHKOUT beforehand.)
; Input: A = Byte to write.
; Output: –
; Used registers: –
; Real address: ($0326), $F1CA.
; 
ultichrout  cmp OUR_DEVICE
            beq _my_chrout
            pla
            jmp ciout ; to serial bus

_my_chrout
            inc MY_OUTLEN
            lda MY_OUTLEN
            beq _breakup_out

_co1        pla
            sta CMD_IF_COMMAND ; Append the byte to write to the current command
            clc
            rts
_breakup_out
            txa
            pha
            jsr uci_execute    ; Execute the complete command, e.g. write the block of data
            jsr uci_ack
            jsr do_chkout      ; Send a new command to start transmission of the next block
            pla
            tax
            jmp _co1


; $FFC6   
; CHKIN. Define file as default input. (Must call OPEN beforehand.)
; Input: X = Logical number.
; Output: –
; Used registers: A, X.
; Real address: ($031E), $F20E.
; 
ultichkin_c
            ldx CMD_IF_COMMAND
            cpx #UCI_IDENTIFIER
            bne _jx11

ultichkin   cmp OUR_DEVICE
            beq _my_chkin
_jx11       cmp #3
            beq _jx320       ;is screen...done.
            jmp jx314
_jx320      jmp jx320

_my_chkin   sta dfltn
            ; check if last command was also CHKIN
            ldx #UCI_CMD_CHKIN
            cpx UCI_LAST_CMD
            bne _chkin_su ; No? Just start the command

            ; last command was CHKIN; it may still be pending
            ; Same secondary address?
            lda SECADDR
            cmp UCI_LAST_SA
            beq _chkin_cont ; Yes? Just do nothing

_chkin_su   jsr uci_setup_cmd
            jsr uci_execute
_chkin_cont clc
            rts

; $FFC9   
; CHKOUT. Define file as default output. (Must call OPEN beforehand.)
; Input: X = Logical number.
; Output: –
; Used registers: A, X.
; Real address: ($0320), $F250.
; 
ultichkout_c
            ldx CMD_IF_COMMAND
            cpx #UCI_IDENTIFIER
            bne _ck11

ultichkout  cmp OUR_DEVICE
            beq _my_chkout
_ck11       cmp #3
            beq _ck30       ;is screen...done.
            jmp ck14
_ck30       jmp ck30

_my_chkout  sta dflto
            bit UCI_PENDING_CMD
            bpl do_chkout   ; No command pending, so setup is always required

            ; check if last command was also CHKOUT
            ldx #UCI_CMD_CHKOUT
            cpx UCI_LAST_CMD
            bne do_chkout   ; Last command not CHKOUT? Setup is required

            ; there is a pending CKOUT command, same SA?
            lda SECADDR
            cmp UCI_LAST_SA
            beq _ckout_cont ; Yes!  Do nothing, just append

do_chkout   lda #0
            sta MY_OUTLEN
            ldx #UCI_CMD_CHKOUT
            jsr uci_setup_cmd ; do not execute command, because we are waiting for data now
_ckout_cont clc
            rts

; $FFCC   
; CLRCHN. Close default input/output files (for serial bus, send UNTALK and/or UNLISTEN); restore default input/output to keyboard/screen.
; Input: –
; Output: –
; Used registers: A, X.
; Real address: ($0322), $F333.
; 
ulticlrchn_lsn
            lda dflto
            cmp OUR_DEVICE
            beq _my_clrchn
            jmp unlsn ; if it was not us, it is serial
_my_clrchn  clc
            rts
;jmp uci_abort

ulticlrchn_tlk
            lda dfltn
            cmp OUR_DEVICE
            beq _my_clrchn
            jmp untlk ; if it was not us, it is serial

;; UCI

uci_setup_cmd
            jsr uci_abort ; will abort any pending command first

            ; clean slate
            lda #$80
            sta UCI_PENDING_CMD ; Bit 7 is now set

            lda #UCI_TARGET
            sta CMD_IF_COMMAND
            stx CMD_IF_COMMAND
            stx UCI_LAST_CMD
            lda SECADDR
            sta CMD_IF_COMMAND
            sta UCI_LAST_SA
            lda VERIFYFLAG
            sta CMD_IF_COMMAND
            rts

uci_setup_range
            lda $00,y
            sta CMD_IF_COMMAND
            lda $01,y
            sta CMD_IF_COMMAND
            lda SAVEEND
            sta CMD_IF_COMMAND
            lda SAVEEND+1
            sta CMD_IF_COMMAND
            rts

uci_filename
            lda NAMELEN
            beq _fn2
            ldy #$00
_fn1        lda (NAMEPTR),y
            sta CMD_IF_COMMAND
            iny
            cpy NAMELEN
            bne _fn1
_fn2        jmp uci_execute

uci_execute lda #CMD_PUSH_CMD
            sta CMD_IF_CONTROL
            lda #0
            sta UCI_PENDING_CMD ; bit 7 is now cleared
            ; intentional fall through
uci_wait_busy
_wb1        lda CMD_IF_CONTROL
            and #CMD_STATE_BITS
            cmp #CMD_STATE_BUSY
            beq _wb1
            ; we should now be in the data state, where we can also read the status
            rts

uci_ack     pha
            lda #CMD_NEXT_DATA
            sta CMD_IF_CONTROL
_ack1       lda CMD_IF_CONTROL
            and #CMD_NEXT_DATA
            bne _ack1
            pla
            rts

uci_abort_c
            lda CMD_IF_COMMAND
            cmp #UCI_IDENTIFIER
            beq uci_abort
            rts

uci_abort   ; may be in command state
            bit UCI_PENDING_CMD
            bpl _abrt1 ; Bit 7 is not set, so there is no pending command
            jsr uci_execute

_abrt1      lda #$00
            sta UCI_LAST_CMD
            lda CMD_IF_CONTROL
            and #CMD_STATE_BITS
            beq _abrt2 ; UCI is already in idle state; we're done

;            cmp #CMD_STATE_MORE_DATA  ; only if it is 'more data', we use abort, otherwise data is just acked.
;            bne uci_ack ; NOT in More Data state, just ack

            ; Perform Abort of current command
            lda #CMD_ABORT
            sta CMD_IF_CONTROL
_wa1        lda CMD_IF_CONTROL
            and #CMD_ABORT
            bne _wa1
_abrt2      rts

uci_clear_error
            lda #CMD_ERROR
            sta CMD_IF_CONTROL
            rts

;------------------------------- Basic Extension -----------------------------------
            AT_TOKEN = $40
            STRING_TOKEN = $24
            CHRGET = $0073
            CHRGOT = $0079
            LINNUM = $14

.import ngone
.import frmevl
.import frmnum
.import getadr
.importzp valtyp
.import frefac
.import newstt
.import linprt

.global wedged_execute

at_command
            plp
            lda #0
            sta STATUS

            jsr CHRGET
            beq _read_status
            jsr frmevl ; Evaluate Expression

            bit valtyp ; String?
            bmi _copy_string_to_dev

            jsr get_dev_num

            jsr CHRGOT
            beq _read_status    ; No second parameter
            cmp #$2c            ; comma
            bne _syntax         ;  has to be present

            jsr CHRGET
            jsr frmevl          ; Evaluate Expression
            bit valtyp          ; second param is a string?
            bpl _syntax         ; no? then it's a syntax error

_copy_string_to_dev
            ldx #$6F
            jsr listen_or_error
            jsr frefac          ; Fetch string parameters
            jsr send_string
            jsr nclrch
            jsr uci_abort_c     ; avoid leaving a command open

_read_status
            ldx #$6F
            jsr listen_or_error
            jsr nclrch
            jsr nchkin_known_fasa

_nxt_st     jsr basin
            ;bcs _status_done
            jsr prt
            cmp #$0D
            bne _nxt_st
_status_done
            jsr nclrch
            jsr uci_abort_c   ; avoid leaving a command open

_next_statement
            jsr CHRGOT
            beq _complete
            cmp #$3a
            bne _syntax
_complete   jmp newstt

_syntax     ldx #$0b
            .byte $2c
_illegal    ldx #$09
            .byte $2c
_not_pres
            ldx #$05
            jmp ($0300)
_kernal_error
            tax
            jmp ($0300)

wedged_execute
            jsr CHRGET
            php
            cmp #AT_TOKEN
            beq at_command
            cmp #STRING_TOKEN
            beq dir_command
			plp
            jmp ngone+3 ;$a7e7

get_dev_num jsr getadr
            lda LINNUM+1
            bne _illegal
            lda LINNUM
            sta DEVNUM
            beq _illegal
            rts

send_string
            tax
            ldy #$00
            inx
_nxtchr     dex
            beq _str_done
            lda ($22),y
_wrcmdbyte  jsr bsout
            iny
            jmp _nxtchr
_str_done   rts

listen_or_error
            stx SECADDR
            lda DEVNUM
            bne _dev_ok
            lda OUR_DEVICE
            sta DEVNUM
_dev_ok     cmp #31
            bcs _illegal

            cmp OUR_DEVICE
            beq _uci_listen
            ; Do it the raw way
            tax
            jsr listn
            lda SECADDR
            jsr secnd
            bit status
            bmi _not_pres
            stx dflto
            rts
_uci_listen jmp ultichkout_c

dir_command
            plp
            lda #0
            sta STATUS
            pha          ; Pre-store length = 0 on stack

            jsr CHRGET
            beq _do_dir  ; no params, default drive, no pattern

            jsr frmevl   ; Evaluate Expression
            bit valtyp   ; String?
            bmi _dir_str ; It's a string, process it

_dir_drv    jsr get_dev_num
            jsr CHRGOT
            beq _do_dir
            cmp #$2c   ; comma
            bne _dir_syntax

            jsr CHRGET
            jsr frmevl   ; Evaluate Expression
            bit valtyp
            bmi _dir_str ; If it's a string, it's OK
_dir_syntax jmp _syntax

_dir_str    pla          ; Ditch 0 on stack
            jsr frefac   ; Fetch string parameters
            pha          ; Replace length with one from string
_do_dir
            jsr nclrch
            ldx #$F0     ; open channel 0
            jsr listen_or_error

            lda #$24
            jsr bsout
            pla          ; Fetch string length from stack
            jsr send_string
            jsr nclrch ; unlisten
            jsr print_dir
            jsr nclrch
            jsr uci_abort_c
            jmp _next_statement

print_dir
            lda #$60
            sta SECADDR
            jsr nchkin_known_fasa
            jsr basin
            jsr basin

_dir_line   jsr basin
            jsr basin
            jsr basin
            tax
            jsr basin
            ldy STATUS
            bne _dir_end

            jsr linprt      ; line number
            lda #' '        ; space
            jsr prt

_dir_char   jsr basin
            beq _dir_cr
            jsr prt
            bne _dir_char

_dir_cr     lda #$0d
            jsr prt         ; newline
            jsr kbget
            cmp  #3 ; STOP
            bne _dir_line

_dir_end    jsr nclrch
            ldx #$E0
            jmp listen_or_error

