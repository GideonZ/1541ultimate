; net_read_timing.asm - times READ_SOCKET on the C64 itself, for net_target_test.py.
;
; A READ that waits and one that does not differ by 40 ms, and the host cannot
; see that: a single READ costs it 0.1-0.4 s of request round trips, and that
; varies by more than the difference. This program issues the READs from the
; 6510, at the UCI registers like any C64 program, and counts PHI2 cycles on the
; CIA2 timers, so the figures are the machine's own.
;
; It times two kinds of 5-byte READ, each READS_PER_BLOCK times in a row, for
; ROUNDS rounds, with the kinds in turn so that drift reaches both:
;   0  READ on a handle the target does not own: answers at once, and is
;      the reference for what one READ costs when it does not wait
;   1  READ on the open, empty socket: must answer at once as well
; Each block's cycle count and the first bytes of every status text are left in
; RAM for the host.
;
; Protocol with the host
;   1. host runs this with runners:run_prg; the program sets READY to $A5
;   2. host writes the open socket's handle to HANDLE and a handle the target
;      does not own to UNOWNED, then writes a non-zero value to GO
;   3. the program runs, writes the results and sets DONE to $A5
;
; Interrupts stay off and the KERNAL is never called, so the CIA2 timers and
; the zero page below are the program's own. Status bytes are popped exactly
; once each (a read of $DF1F consumes its byte) and never retried.

CTRL    = $DF1C                 ; write: control, read: status
CMDREG  = $DF1D                 ; write: command byte queue
RESP    = $DF1E                 ; read: response data queue
STATR   = $DF1F                 ; read: status data queue

CTRL_PUSH = $01
CTRL_ACC  = $02
CTRL_CLR  = $08

ST_STATE  = $30
ST_LAST   = $20
ST_MORE   = $30
ST_STAT   = $40
ST_DATA   = $80

READS_PER_BLOCK = 20
ROUNDS          = 3
KINDS           = 2
SLOT_BYTES      = 16            ; first bytes of each status text, zero padded
WAIT_WRAPS      = $08           ; as in uci_agent.asm: about six seconds

; ---------------------------------------------------------------- the block --
GO       = $C000                ; host sets non-zero to start
READY    = $C001                ; $A5 once the program is running
DONE     = $C002                ; $A5 once the results are complete
HANDLE   = $C003                ; the open socket
UNOWNED  = $C004                ; a handle the target does not own
FLAGS    = $C005                ; bit 0: a READ never answered, bit 1: the interface would not go Idle
BLOCKS   = $C010                ; ROUNDS x KINDS cycle counts, 4 bytes each, low byte first
STATUSES = $C100                ; ROUNDS x KINDS x READS_PER_BLOCK x SLOT_BYTES

; -------------------------------------------------------------- zero page ----
zdst    = $FB                   ; where the current status text goes (2)
zkind   = $FD
zround  = $FE
zi      = $F7
zcl     = $F8                   ; command length of the current kind
ztmo    = $E0                   ; 24 bit wait counter (3)
ztmp    = $E3
tm0     = $20                   ; the timer, as read
tm1     = $21
tm2     = $22
tm3     = $23
ts0     = $24                   ; the timer at the start of the block
ts1     = $25
ts2     = $26
ts3     = $27
zblk    = $28                   ; pointer to this block's result (2)

        * = $0801

; BASIC line "10 SYS 2061", so the host can start this with runners:run_prg.
        .word basic_end, 10
        .byte $9e
        .text "2061"
        .byte 0
basic_end
        .word 0

start
        sei
        lda #$00
        sta GO
        sta DONE
        sta FLAGS
        jsr clear_block
        jsr timer_init
        jsr release
        lda #$A5
        sta READY
main_wait
        lda GO
        beq main_wait

        ; zblk walks BLOCKS, zdst walks STATUSES; both advance in step with the
        ; (round, kind) order, one block after another.
        lda #<BLOCKS
        sta zblk
        lda #>BLOCKS
        sta zblk+1
        lda #<STATUSES
        sta zdst
        lda #>STATUSES
        sta zdst+1
        lda #$00
        sta zround
round_loop
        lda #$00
        sta zkind
kind_loop
        jsr set_command
        jsr rdtime
        jsr copy_start
        lda #$00
        sta zi
read_loop
        jsr read_once
        clc                     ; next status slot
        lda zdst
        adc #SLOT_BYTES
        sta zdst
        bcc slot_ok
        inc zdst+1
slot_ok
        inc zi
        lda zi
        cmp #READS_PER_BLOCK
        bne read_loop

        jsr rdtime
        jsr store_elapsed
        clc                     ; next result
        lda zblk
        adc #$04
        sta zblk
        bcc blk_ok
        inc zblk+1
blk_ok
        inc zkind
        lda zkind
        cmp #KINDS
        bne kind_loop
        inc zround
        lda zround
        cmp #ROUNDS
        bne round_loop

        lda #$A5
        sta DONE
spin
        jmp spin

; ------------------------------------------------------------ clear_block ---
; The host reads RAM the machine does not clear, so a stale block from an
; earlier run would otherwise look like this one's.
clear_block
        lda #<BLOCKS
        sta zdst
        lda #>BLOCKS
        sta zdst+1
        ldy #$00
cb_blocks
        lda #$00
        sta (zdst),y
        iny
        cpy #ROUNDS*KINDS*4
        bne cb_blocks
        lda #<STATUSES
        sta zdst
        lda #>STATUSES
        sta zdst+1
        ldx #(ROUNDS*KINDS*READS_PER_BLOCK*SLOT_BYTES)/256+1
        ldy #$00
cb_status
        lda #$00
        sta (zdst),y
        iny
        bne cb_status
        inc zdst+1
        dex
        bne cb_status
        rts

; ------------------------------------------------------------- set_command ---
; Builds the command for the current kind: $03 $10 handle len-lo len-hi.
set_command
        lda #$03                ; network target
        sta cmd
        lda #$10                ; READ_SOCKET
        sta cmd+1
        lda HANDLE
        ldx zkind
        bne sc_handle
        lda UNOWNED
sc_handle
        sta cmd+2
        lda #$40                ; 64 bytes is more than an empty reply carries
        sta cmd+3
        lda #$00
        sta cmd+4
        lda #$05
        sta zcl
        rts

; --------------------------------------------------------------- read_once ---
; One READ, followed through to its accept, its status text stored at (zdst).
read_once
        ldx #$00
ro_push
        lda cmd,x
        sta CMDREG
        inx
        cpx zcl
        bne ro_push
        lda #CTRL_PUSH
        sta CTRL

        lda #$00
        sta ztmo
        sta ztmo+1
        sta ztmo+2
ro_wait
        lda CTRL
        and #ST_STATE
        cmp #ST_LAST
        beq ro_reply
        cmp #ST_MORE
        beq ro_reply
        inc ztmo
        bne ro_wait
        inc ztmo+1
        bne ro_wait
        inc ztmo+2
        lda ztmo+2
        cmp #WAIT_WRAPS
        bne ro_wait
        lda FLAGS               ; never answered: leave it for the host to report
        ora #$01
        sta FLAGS
        jmp release

ro_reply
ro_data
        lda CTRL                ; the data is not wanted, only taken off the queue
        and #ST_DATA
        beq ro_status
        lda RESP
        jmp ro_data
ro_status
        ldy #$00
ro_stat_loop
        lda CTRL
        and #ST_STAT
        beq ro_stat_done
        lda STATR               ; one pop per byte, never repeated
        cpy #SLOT_BYTES
        bcs ro_stat_loop        ; popped, not stored
        sta (zdst),y
        iny
        jmp ro_stat_loop
ro_stat_done
        lda #CTRL_ACC
        sta CTRL

; ----------------------------------------------------------------- release ---
; Hands back anything pending and clears a stale error, until the interface is
; Idle. Bounded, so a state that will not clear is recorded and not hung on.
release
        ldx #$20
rel_loop
        lda CTRL
        and #ST_STATE
        beq rel_done
        lda #CTRL_ACC
        sta CTRL
        lda #CTRL_CLR
        sta CTRL
        dex
        bne rel_loop
        lda FLAGS               ; still not Idle: recorded for the host
        ora #$02
        sta FLAGS
rel_done
        rts

; ---------------------------------------------------------------- the timer ---
; CIA2 timer A counts PHI2 and timer B counts A's underflows: one 32 bit down
; counter, as tls_bench.asm uses.
timer_init
        lda #$7F
        sta $DD0D               ; no CIA2 interrupts
        lda #$FF
        sta $DD04
        sta $DD05
        sta $DD06
        sta $DD07
        lda #$10
        sta $DD0E               ; force load A, stopped
        lda #$50
        sta $DD0F               ; force load B, counts A underflows, stopped
        lda #$01
        sta $DD0E               ; start A
        lda #$41
        sta $DD0F               ; start B
        rts

; rdtime: the counter into tm0..tm3 (A lo, A hi, B lo, B hi), retried until stable.
rdtime
rt_again
        ldx $DD06
        ldy $DD07
        lda $DD05
        sta ztmp
        lda $DD04
        sta tm0
        lda $DD05
        cmp ztmp
        bne rt_again
        cpx $DD06
        bne rt_again
        cpy $DD07
        bne rt_again
        sta tm1
        stx tm2
        sty tm3
        rts

copy_start
        lda tm0
        sta ts0
        lda tm1
        sta ts1
        lda tm2
        sta ts2
        lda tm3
        sta ts3
        rts

; store_elapsed: start - now, as the counter counts down, at (zblk).
store_elapsed
        sec
        ldy #$00
        lda ts0
        sbc tm0
        sta (zblk),y
        iny
        lda ts1
        sbc tm1
        sta (zblk),y
        iny
        lda ts2
        sbc tm2
        sta (zblk),y
        iny
        lda ts3
        sbc tm3
        sta (zblk),y
        rts

cmd
        .fill 5
