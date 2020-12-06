    .segment "RS232"

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

chrout      pha

            lda dflto
            cmp #7;OUR_DEVICE
            beq my_chrout
            pla
            ;jmp (origvect_chrout)

my_chrout   inc MY_OUTLEN
            lda MY_OUTLEN
            beq _breakup_out

_a          pla
            sta CMD_IF_COMMAND ; Append the byte to write to the current command
            clc
            rts
_breakup_out
            txa
            pha
            ;jsr uci_execute    ; Execute the complete command, e.g. write the block of data
            ;jsr uci_ack
            ;jsr do_chkout      ; Send a new command to start transmission of the next block
            pla
            tax
            jmp _a
