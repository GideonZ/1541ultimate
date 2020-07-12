;-------------------------------------------------
; Test routines for loading / saving through UCI
;
; Programmed by Gideon Zweijtzer
;
; Copyright (c) 2020 - Gideon Zweijtzer
;
;-------------------------------------------------
;
; This module installs some basic extensions to test the UCI load / save functions.
; The load/save functions are hooked by vectors and should fall through to UCI
; when the device number is 7.
    .feature labels_without_colons, pc_assignment
    .segment "LADDR"
    .word $c000
    .segment "CODE"

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

            OUR_DEVICE = $96
            
            *= $C000            ;base of wedge
            
            jmp install
            jmp send_long_string
            jmp load_file_using_chrin
            
            JSETLFS = $FFBA 
            JSETNAM = $FFBD
            JOPEN   = $FFC0
            JCLOSE  = $FFC3
            JCHKIN  = $FFC6
            JCHKOUT = $FFC9
            JCLRCHN = $FFCC
            JCHRIN  = $FFCF
            JCHROUT = $FFD2
            JLOAD   = $FFD5
            JSAVE   = $FFD8

            OPEN_VECTOR   = $031A
            CLOSE_VECTOR  = $031C
            CHKIN_VECTOR  = $031E
            CHKOUT_VECTOR = $0320
            CLRCHN_VECTOR = $0322
            CHRIN_VECTOR  = $0324
            CHROUT_VECTOR = $0326
            GETIN_VECTOR  = $032A
            LOAD_VECTOR   = $0330
            SAVE_VECTOR   = $0332

            STATUS     = $90
            VERIFYFLAG = $93
            DEVFROM    = $99
            DEVTO      = $9A
            LOADPNTR   = $AE
            SECADDR    = $B9
            DEVNUM     = $BA
            NAMEPTR    = $BB
            NAMELEN    = $B7
            LOADADDR   = $C3            
            SAVEADDR   = $C1
            SAVEEND    = $AE
            MY_OUTLEN  = $0276 ; Last byte of logical file table 
            
            FILE_LOOKUP_A           = $F314
            FILE_LOOKUP_X           = $F30F ; Might be different for JD!
            FILE_NOT_OPEN_ERROR     = $F701 ; Might be different for JD!
            GET_FILE_PARAMS         = $F31F ; Might be different for JD!
            CHKIN_CONTINUED         = $F219 ; Might be different for JD!
            CHKOUT_CONTINUED        = $F25B ; Might be different for JD!
            CLRCHN_CONTINUED        = $F343 ; Might be different for JD! Call with X = $03
            CHROUT_SCREEN           = $E716
            FILE_NOT_FOUND_ERROR    = $F704
            MISSING_FILENAME_ERROR  = $F710
            SHOW_SEARCHING          = $F5AF            
            SHOW_LOADING            = $F5D2
            SHOW_SAVING             = $F68F

install
            lda LOAD_VECTOR
            ldy LOAD_VECTOR+1
            sta origvect_load
            sty origvect_load+1
            lda #<load
            ldy #>load
            sta LOAD_VECTOR
            sty LOAD_VECTOR+1

            lda SAVE_VECTOR
            ldy SAVE_VECTOR+1
            sta origvect_save
            sty origvect_save+1
            lda #<save
            ldy #>save
            sta SAVE_VECTOR
            sty SAVE_VECTOR+1

            lda OPEN_VECTOR
            ldy OPEN_VECTOR+1
            sta origvect_open
            sty origvect_open+1
            lda #<open
            ldy #>open
            sta OPEN_VECTOR
            sty OPEN_VECTOR+1

            lda CLOSE_VECTOR
            ldy CLOSE_VECTOR+1
            sta origvect_close
            sty origvect_close+1
            lda #<close
            ldy #>close
            sta CLOSE_VECTOR
            sty CLOSE_VECTOR+1

            lda GETIN_VECTOR
            ldy GETIN_VECTOR+1
            sta origvect_getin
            sty origvect_getin+1
            lda #<getin
            ldy #>getin
            sta GETIN_VECTOR
            sty GETIN_VECTOR+1

            lda CHRIN_VECTOR
            ldy CHRIN_VECTOR+1
            sta origvect_chrin
            sty origvect_chrin+1
            lda #<chrin
            ldy #>chrin
            sta CHRIN_VECTOR
            sty CHRIN_VECTOR+1

            lda CHROUT_VECTOR
            ldy CHROUT_VECTOR+1
            sta origvect_chrout
            sty origvect_chrout+1
            lda #<chrout
            ldy #>chrout
            sta CHROUT_VECTOR
            sty CHROUT_VECTOR+1

            lda CHKIN_VECTOR
            ldy CHKIN_VECTOR+1
            sta origvect_chkin
            sty origvect_chkin+1
            lda #<chkin
            ldy #>chkin
            sta CHKIN_VECTOR
            sty CHKIN_VECTOR+1

            lda CHKOUT_VECTOR
            ldy CHKOUT_VECTOR+1
            sta origvect_chkout
            sty origvect_chkout+1
            lda #<chkout
            ldy #>chkout
            sta CHKOUT_VECTOR
            sty CHKOUT_VECTOR+1

            lda CLRCHN_VECTOR
            ldy CLRCHN_VECTOR+1
            sta origvect_clrchn
            sty origvect_clrchn+1
            lda #<clrchn
            ldy #>clrchn
            sta CLRCHN_VECTOR
            sty CLRCHN_VECTOR+1

            lda #9
            sta OUR_DEVICE

            jmp uci_clear_error


origvect_load    .word 0
origvect_save    .word 0
origvect_open    .word 0
origvect_close   .word 0
origvect_getin   .word 0
origvect_chrin   .word 0
origvect_chrout  .word 0
origvect_chkin   .word 0
origvect_chkout  .word 0
origvect_clrchn  .word 0

; $FFD5 
; LOAD. Load or verify file. (Must call SETLFS and SETNAM beforehand.)
; Input: A: 0 = Load, 1-255 = Verify; X/Y = Load address (if secondary address = 0).
; Output: Carry: 0 = No errors, 1 = Error; A = KERNAL error code (if Carry = 1); X/Y = Address of last byte loaded/verified (if Carry = 0).
; Used registers: A, X, Y.
; Real address: $F49E.
; Vectors through $0330, after storing X in $C3 and Y in $C4 (LOADADDR)

load
            ldx DEVNUM
            cpx OUR_DEVICE
            beq myload
ld1         jmp (origvect_load)            

myload      sta VERIFYFLAG
            lda #$00
            sta STATUS
            ldy NAMELEN
            bne ld2
            jmp MISSING_FILENAME_ERROR

ld2         lda CMD_IF_COMMAND
            cmp #UCI_IDENTIFIER
            bne ld1

            ldx SECADDR
            jsr SHOW_SEARCHING
            ldx #UCI_CMD_LOADSU
            jsr uci_setup_cmd
            ldy #LOADADDR
            jsr uci_setup_range
            jsr uci_filename ; also executes
            lda CMD_IF_STATUS
            jsr uci_ack ; restores A
            beq ld3 ; all OK when zero
            jmp FILE_NOT_FOUND_ERROR

ld3         jsr SHOW_LOADING

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

save
            lda DEVNUM
            cmp OUR_DEVICE
            beq mysave
sv1         jmp (origvect_save)            

mysave
            lda #$00
            sta STATUS
            ldy NAMELEN
            bne sv2
            jmp MISSING_FILENAME_ERROR

sv2         lda CMD_IF_COMMAND
            cmp #UCI_IDENTIFIER
            bne sv1

            jsr SHOW_SAVING

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

open        lda DEVNUM
            cmp OUR_DEVICE
            beq myopen
op1         jmp (origvect_open)            
        
myopen      lda CMD_IF_COMMAND
            cmp #UCI_IDENTIFIER
            bne op1

            ; The following is a copy of the kernal code at F34A, because
            ; it is common code, which should always be executed, but it
            ; cannot be vectored. Not needed in modified kernal, as the check
            ; for the device number can be placed AFTER this code.

            lookup = $F30F
            error1 = $F6FB
            error2 = $F6FE
            error6 = $F70A
            ldtnd  = $98
            la     = $B8
            sa     = $B9
            fa     = $BA
            lat    = $0259
            sat    = $026D
            fat    = $0263
                        
nopen       ldx la          ;check file #
            bne op98        ;is not the keyboard
            jmp error6      ;not input file...
op98        jsr lookup      ;see if in table
            bne op100       ;not found...o.k.
            jmp error2      ;file open
op100       ldx ldtnd       ;logical device table end
            cpx #10         ;maximum # of open files
            bcc op110       ;less than 10...o.k.
            jmp error1      ;too many files
op110       inc ldtnd       ;new file
            lda la
            sta lat,x       ;store logical file #
            lda sa
            ora #$60        ;make sa an serial command
            sta sa
            sta sat,x       ;store command #
            lda fa
            sta fat,x       ;store device #

            ldx #UCI_CMD_OPEN
            jsr uci_setup_cmd
            jsr uci_filename ; also executes                  
            jsr uci_ack
            
            clc
            rts

; $FFC3   
; CLOSE. Close file.
; Input: A = Logical number.
; Output: –
; Used registers: A, X, Y.
; Real address: ($031C), $F291.
;

close       pha
            jsr FILE_LOOKUP_A
            beq cl1
            pla
            clc
            rts

            ; x is now set to index and stack has original a
cl1         jsr GET_FILE_PARAMS
            
            lda DEVNUM
            cmp OUR_DEVICE
            beq myclose
cl2         pla ; restore stack for exit
            jmp (origvect_close)   ; And do the lookup again. ;)            
        
myclose     lda CMD_IF_COMMAND
            cmp #UCI_IDENTIFIER
            bne cl2
            pla ; restore stack, but we don't need a

            ldx #UCI_CMD_CLOSE
            jsr uci_setup_cmd
            jsr uci_execute
            jsr uci_ack
            clc
            rts

; $FFE4   
; GETIN. Read byte from default input. (If not keyboard, must call OPEN and CHKIN beforehand.)
; Input: –
; Output: A = Byte read.
; Used registers: A, X, Y.
; Real address: ($032A), $F13E.


getin       lda DEVFROM
            cmp OUR_DEVICE
            beq my_chrin
            jmp (origvect_getin)


; $FFCF   
; CHRIN. Read byte from default input (for keyboard, read a line from the screen). (If not keyboard, must call OPEN and CHKIN beforehand.)
; Input: –
; Output: A = Byte read.
; Used registers: A, Y.
; Real address: ($0324), $F157.
 

chrin       lda DEVFROM
            cmp OUR_DEVICE
            beq my_chrin
            jmp (origvect_chrin)

my_chrin    lda CMD_IF_CONTROL
            bpl _no_data_avail
            lda CMD_IF_RESULT
            pha
            lda CMD_IF_CONTROL
            bmi _ok            
            and #CMD_STATE_BITS
            cmp #CMD_STATE_LAST_DATA
            bne _ok
            jmp _eof
_ok         pla
            clc
            rts ; done!!            

_no_data_avail
            and #CMD_STATE_BITS
            cmp #CMD_STATE_LAST_DATA
            beq _end_of_file

            ; Get next block of data
            jsr uci_ack                                         
            jsr uci_wait_busy
            jmp my_chrin

_end_of_file
            lda #$0D
            pha
_eof
            lda #$40
            ora STATUS
            sta STATUS
            pla
            clc
            rts


; $FFD2   
; CHROUT. Write byte to default output. (If not screen, must call OPEN and CHKOUT beforehand.)
; Input: A = Byte to write.
; Output: –
; Used registers: –
; Real address: ($0326), $F1CA.
; 
chrout      pha

            lda DEVTO
            cmp OUR_DEVICE
            beq _my_chrout
            pla
            jmp (origvect_chrout)

_my_chrout  inc MY_OUTLEN
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
chkin       jsr FILE_LOOKUP_X       ; as copied from stock kernal
            beq _cki1               ; as copied from stock kernal
            jmp FILE_NOT_OPEN_ERROR ; as copied from stock kernal
_cki1       jsr GET_FILE_PARAMS     ; as copied from stock kernal
            lda DEVNUM
            cmp OUR_DEVICE
            beq _my_chkin
            jmp CHKIN_CONTINUED    ; continue at stock kernal location

_my_chkin   sta DEVFROM
do_chkin    ldx #UCI_CMD_CHKIN
            jsr uci_setup_cmd
            jsr uci_execute
            clc
            rts

; $FFC9   
; CHKOUT. Define file as default output. (Must call OPEN beforehand.)
; Input: X = Logical number.
; Output: –
; Used registers: A, X.
; Real address: ($0320), $F250.
; 
chkout      jsr FILE_LOOKUP_X       ; as copied from stock kernal
            beq _cko1               ; as copied from stock kernal
            jmp FILE_NOT_OPEN_ERROR ; as copied from stock kernal
_cko1       jsr GET_FILE_PARAMS     ; as copied from stock kernal
            lda DEVNUM
            cmp OUR_DEVICE
            beq _my_chkout
            jmp CHKOUT_CONTINUED    ; continue at stock kernal location

_my_chkout  sta DEVTO
do_chkout   lda #0
            sta MY_OUTLEN
            ldx #UCI_CMD_CHKOUT
            clc
            jmp uci_setup_cmd       ; do not execute command, because we are waiting for data now

; $FFCC   
; CLRCHN. Close default input/output files (for serial bus, send UNTALK and/or UNLISTEN); restore default input/output to keyboard/screen.
; Input: –
; Output: –
; Used registers: A, X.
; Real address: ($0322), $F333.
; 
clrchn      lda DEVNUM
            cmp OUR_DEVICE
            beq _my_clrchn
            jmp (origvect_clrchn)

_my_clrchn  jsr uci_abort
            ldx #3
            jmp CLRCHN_CONTINUED

;; UCI

uci_setup_cmd
            lda #UCI_TARGET
            sta CMD_IF_COMMAND
            stx CMD_IF_COMMAND
            lda SECADDR
            sta CMD_IF_COMMAND
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

uci_abort   lda CMD_IF_CONTROL
            and #CMD_STATE_DATA
            bne _abrt1
            ; Not in data state, but may be in command state
            ; So send command, even if it may be an empty command
            lda #CMD_PUSH_CMD
            sta CMD_IF_CONTROL
            jsr uci_wait_busy
            jmp uci_ack
_abrt1      lda #CMD_ABORT
            sta CMD_IF_CONTROL
            jmp uci_wait_abort

uci_wait_abort
_wa1        lda CMD_IF_CONTROL
            and #CMD_ABORT
            bne _wa1
            rts

uci_clear_error
            lda #CMD_ERROR
            sta CMD_IF_CONTROL
            rts
                           
;; Print some info
printinfo   ldy #$00
_pi1        lda (NAMEPTR),y
            jsr CHROUT_SCREEN
            iny
            cpy NAMELEN
            bne _pi1
            lda #$2C
            jsr CHROUT_SCREEN
            
            lda SECADDR
            jsr hexout
            lda #$2C
            jsr CHROUT_SCREEN
            lda LOADADDR+1
            jsr hexout
            lda LOADADDR
            jsr hexout
            lda #$2C
            jsr CHROUT_SCREEN
            lda VERIFYFLAG
            jsr hexout
            lda #$0D
            jmp CHROUT_SCREEN

show_end_addr
            lda #$20
            jsr CHROUT_SCREEN
            lda LOADPNTR+1
            jsr hexout
            lda LOADPNTR
            jmp hexout

hexout      stx $02
            pha
            pha
            and #$F0
            lsr
            lsr
            lsr
            lsr
            tax
            lda hex_chars,x
            jsr CHROUT_SCREEN
            pla
            and #$0f
            tax
            lda hex_chars,x
            jsr CHROUT_SCREEN
            ldx $02
            pla
            rts
            
hex_chars   .asciiz "0123456789ABCDEF"

filename    .asciiz "AAP"
send_long_string 
            lda #7
            tax
            tay
            jsr JSETLFS
            lda #3
            ldx #<filename
            ldy #>filename
            jsr JSETNAM
            jsr JOPEN      ; OPEN 7,7,7,"AAP"

            ldx #7         
            jsr JCHKOUT

            ldx #$FF
_sls1       txa
            jsr JCHROUT
            dex
            bne _sls1 

            ldx #$FF
_sls2       txa
            jsr JCHROUT
            dex
            bne _sls2

            jsr JCLRCHN

            lda #7          ; CLOSE 7
            jsr JCLOSE
            rts

filename2   .asciiz "UD- 1,S"

load_file_using_chrin
        lda #7
        tay
        ldx OUR_DEVICE
        jsr JSETLFS
        lda #7
        ldx #<filename2
        ldy #>filename2
        jsr JSETNAM
        jsr JOPEN      ; OPEN 7,#,7,"UD- 1,S"

        ldx #7
        jsr JCHKIN

        lda #0
        sta $fb
        sta $fc
        
_in1    jsr JCHRIN
        ;jsr CHROUT_SCREEN
        cmp #13
        bne _in2
        inc $fb
        bne _in2
        inc $fc
_in2    lda STATUS
        and #64
        beq _in1

        jsr JCLRCHN
        lda #7
        jsr JCLOSE
        rts
        