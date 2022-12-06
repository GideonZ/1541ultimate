;-------------------------------------------------
; Test routines UCI
;
; Programmed by Gideon Zweijtzer
;
; Copyright (c) 2020 - Gideon Zweijtzer
;
;-------------------------------------------------
    *=$0801
    .word ss,2005
    .null $9e,format("%d", start);sys
ss  .word 0

            CMD_IF_CONTROL = $DF1C
            CMD_IF_COMMAND = $DF1D
            CMD_IF_RESULT  = $DF1E
            CMD_IF_STATUS  = $DF1F

            UCI_IDENTIFIER = $C9
            CMD_PUSH_CMD   = $81
            CMD_NEXT_DATA  = $02
            CMD_ABORT      = $04
            CMD_ERROR      = $08
            
            CMD_STATE_BITS      = $30
            CMD_STATE_DATA      = $20
            CMD_STATE_IDLE      = $00
            CMD_STATE_BUSY      = $10
            CMD_STATE_LAST_DATA = $20
            CMD_STATE_MORE_DATA = $30

            UCI_TARGET     = $04
            UCI_CMD_FREEZE = $05

            CHROUT_SCREEN           = $E716

start       ;jsr uci_abort
            ldx #UCI_CMD_FREEZE
            jsr uci_setup_cmd
            jsr uci_execute
            jsr get_data
            jsr get_status
            jsr uci_ack
            rts

;; UCI

uci_setup_cmd
            lda #UCI_TARGET
            sta CMD_IF_COMMAND
            stx CMD_IF_COMMAND
            rts

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
            
hex_chars   .text "0123456789ABCDEF"

get_status  .proc
            bit CMD_IF_CONTROL
            bvs +
            rts
+           lda #<status_string
            ldy #>status_string
            jsr $ab1e
-           bit CMD_IF_CONTROL
            bvc +
            lda CMD_IF_STATUS
            jsr CHROUT_SCREEN
            jmp -
+           lda #$0d
            jmp CHROUT_SCREEN
status_string .text "STATUS: ", $00
            .pend

get_data    .proc
            bit CMD_IF_CONTROL
            bmi +
            rts
+           lda #<data_string
            ldy #>data_string
            jsr $ab1e

-           lda CMD_IF_CONTROL
            bpl +
            lda CMD_IF_RESULT
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
            lda #$20
            jsr CHROUT_SCREEN
            jmp -
+           lda #$0D
            jmp CHROUT_SCREEN
data_string .text "DATA: ", $00
            .pend

