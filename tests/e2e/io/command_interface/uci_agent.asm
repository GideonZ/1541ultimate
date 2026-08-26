; uci_agent.asm - a resident 6502 agent that drives the Ultimate Command
; Interface from the C64 side, for the E2E suites under tests/e2e.
;
; A test can also reach $DF1C-$DF1F through REST machine:readmem and
; machine:writemem, which are DMA cycles the Ultimate issues on the cartridge
; bus. This agent is the other path, and the one a real program takes: ordinary
; loads and stores executed by the 6510, microseconds apart rather than tens of
; milliseconds, with no DMA involved. It is also much cheaper, because one
; transaction costs a handful of REST calls however large the reply.
;
; The agent knows nothing about any target. It pushes whatever command bytes it
; is given and records everything the reply exposes: the state byte per block,
; the data, the status text, the bytes a client would read past DATA_AV, and how
; many blocks arrived. A test for Ultimate DOS, SoftIEC, the control target or
; the HTTP target needs different bytes in CMDBUF, not a change here.
;
; Extension points, so that most new tests need none:
;   CMDBUF      any command for any target, up to the 896-byte queue
;   OPT_MAXB    how far to follow a Data More reply
;   OPT_CAP     how much to pull before giving up, which turns a reply that
;               never ends into a measurement instead of a hang
;   OPT_OVR     how many bytes to read past DATA_AV
;   $C009-$C00F and $C440-$C4FF are reserved for further options and results.
; New behaviour, as opposed to a new value, belongs in a reserved option byte,
; where zero has to keep meaning what the agent does today: the host writes the
; whole option block at once, so an older host leaves zeroes there.
;
; Protocol with the host
;   1. host writes the command to CMDBUF and its length to CMDLEN, sets the
;      options, then writes a non-zero value to GO
;   2. agent runs the transaction, writes the results, clears GO, bumps SEQ
;   3. host waits for SEQ to change, then reads the result block
;
; The agent runs with interrupts disabled and never calls the KERNAL, so nothing
; else touches the zero page locations it uses. The host side is
; tests/e2e/lib/uci_native.py, which assembles this with the repository's own
; 64tass and starts it over runners:run_prg.

; ---------------------------------------------------------------- registers --
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

; ------------------------------------------------------------ control block --
GO       = $C000                ; host sets non-zero to start, agent clears
SEQ      = $C001                ; incremented after each completed transaction
CMDLEN   = $C002                ; 16 bit, low byte first
OPT_OVR  = $C004                ; bytes to read from RESP after DATA_AV clears
OPT_MAXB = $C005                ; how many reply blocks to follow, at least 1
OPT_CAP  = $C006                ; 16 bit cap on the data drain
READY    = $C008                ; agent writes $A5 here once it is running
;          $C009-$C00F          ; reserved for further options
CMDBUF   = $C010                ; command bytes, up to 896

; ------------------------------------------------------------- result block --
R_FIRST  = $C400                ; CTRL as the first reply block arrived
R_BLOCKS = $C401                ; blocks collected
R_DLEN   = $C402                ; 16 bit total bytes pulled from RESP
R_SLEN   = $C404                ; status text length
R_FINAL  = $C405                ; CTRL after the last DATA_ACC
R_FLAGS  = $C406                ; 1 wait timed out, 2 drain cap hit, 4 status cap
R_OVRN   = $C410                ; up to 16 bytes read past DATA_AV
R_BSTAT  = $C420                ; CTRL per block, up to 8
R_BLEN   = $C430                ; running total after each block, 8 x 16 bit
;          $C440-$C4FF          ; reserved for further results
STATBUF  = $C500                ; status text, up to 255 bytes
DATABUF  = $2000                ; response data

; -------------------------------------------------------------- zero page ----
zsrc    = $FB                   ; command read pointer
zdst    = $FD                   ; data write pointer
zcnt    = $03                   ; 16 bit scratch counter
zcap    = $05                   ; 16 bit remaining drain allowance
ztmo    = $07                   ; 24 bit wait counter
zblk    = $0A                   ; block index
zstat   = $0B                   ; CTRL as the current block arrived

; The wait gives up after this many wraps of a 16 bit counter. Each pass is
; about a dozen cycles, so $08 is roughly six seconds on a 1 MHz 6510: longer
; than any command takes, shorter than the host's own timeout.
WAIT_WRAPS = $08

        * = $0801

; BASIC line "10 SYS 2061", so the host can start this with runners:run_prg.
        .word basic_end, 10
        .byte $9e
        .text "2061"
        .byte 0
basic_end
        .word 0

; --------------------------------------------------------------------- main --
start
        sei
        lda #$00
        sta GO
        sta SEQ
        lda #$A5
        sta READY
main_wait
        lda GO
        beq main_wait
        jsr transact
        lda #$00
        sta GO
        inc SEQ
        jmp main_wait

; ---------------------------------------------------------------- transact ---
; Runs one command end to end and fills the result block.
transact
        lda #$00
        sta R_FLAGS
        sta R_BLOCKS
        sta R_SLEN
        sta R_DLEN
        sta R_DLEN+1
        sta zblk

        ; Hand back anything still pending and clear a stale error, so the
        ; interface is idle before the command goes in. Bounded, because a
        ; state that will not clear must not hang the agent.
        ldx #$20
release_loop
        lda #CTRL_ACC
        sta CTRL
        lda #CTRL_CLR
        sta CTRL
        lda CTRL
        and #ST_STATE
        beq release_done
        dex
        bne release_loop
release_done

        ; Write the command bytes into the command queue, one at a time.
        lda #<CMDBUF
        sta zsrc
        lda #>CMDBUF
        sta zsrc+1
        lda CMDLEN
        sta zcnt
        lda CMDLEN+1
        sta zcnt+1
push_loop
        lda zcnt
        ora zcnt+1
        beq push_done
        ldy #$00
        lda (zsrc),y
        sta CMDREG
        inc zsrc
        bne push_nohi
        inc zsrc+1
push_nohi
        lda zcnt
        bne push_nodec
        dec zcnt+1
push_nodec
        dec zcnt
        jmp push_loop
push_done
        lda #CTRL_PUSH
        sta CTRL

        jsr wait_reply
        bcs transact_timeout
        lda zstat
        sta R_FIRST

        ; Where the payload goes, and how much of it is allowed.
        lda #<DATABUF
        sta zdst
        lda #>DATABUF
        sta zdst+1
        lda OPT_CAP
        sta zcap
        lda OPT_CAP+1
        sta zcap+1

block_loop
        jsr drain_data
        jsr read_overrun
        jsr drain_status
        jsr record_block

        ; Follow a Data More reply into its next block, up to OPT_MAXB.
        lda zstat
        and #ST_STATE
        cmp #ST_MORE
        bne blocks_done
        lda zblk
        cmp OPT_MAXB
        bcs blocks_done
        lda #CTRL_ACC
        sta CTRL
        jsr wait_reply
        bcs blocks_done
        jmp block_loop

blocks_done
        lda #CTRL_ACC
        sta CTRL
        lda CTRL
        sta R_FINAL
        rts

transact_timeout
        lda CTRL
        sta R_FIRST
        sta R_FINAL
        rts

; -------------------------------------------------------------- wait_reply ---
; Waits for the state bits to reach Data Last or Data More. Leaves CTRL in
; zstat and returns carry clear. On giving up it sets bit 0 of R_FLAGS and
; returns carry set.
wait_reply
        lda #$00
        sta ztmo
        sta ztmo+1
        sta ztmo+2
wait_loop
        lda CTRL
        sta zstat
        and #ST_STATE
        cmp #ST_LAST
        beq wait_ok
        cmp #ST_MORE
        beq wait_ok
        inc ztmo
        bne wait_loop
        inc ztmo+1
        bne wait_loop
        inc ztmo+2
        lda ztmo+2
        cmp #WAIT_WRAPS
        bne wait_loop
        lda R_FLAGS
        ora #$01
        sta R_FLAGS
        sec
        rts
wait_ok
        clc
        rts

; -------------------------------------------------------------- drain_data ---
; Pulls bytes from the response queue for as long as DATA_AV is set, which is
; what the protocol tells a client to do, stopping at OPT_CAP so that a queue
; which never clears cannot hang the machine. Hitting the cap sets bit 1 of
; R_FLAGS, and that is the whole point of the cap: it is a measurement, not a
; safety net.
drain_data
        lda CTRL
        and #ST_DATA
        beq drain_done
        lda zcap
        ora zcap+1
        bne drain_room
        lda R_FLAGS
        ora #$02
        sta R_FLAGS
        rts
drain_room
        lda RESP
        ldy #$00
        sta (zdst),y
        inc zdst
        bne drain_nohi
        inc zdst+1
drain_nohi
        inc R_DLEN
        bne drain_nocarry
        inc R_DLEN+1
drain_nocarry
        lda zcap
        bne drain_nodec
        dec zcap+1
drain_nodec
        dec zcap
        jmp drain_data
drain_done
        rts

; ------------------------------------------------------------ read_overrun ---
; Reads OPT_OVR further bytes from the response queue after DATA_AV has gone
; away, on the first block only. That is what a client sees if it reads a fixed
; count instead of following the flag.
read_overrun
        lda zblk
        bne overrun_done
        ldx #$00
overrun_loop
        cpx OPT_OVR
        beq overrun_done
        cpx #$10
        beq overrun_done
        lda RESP
        sta R_OVRN,x
        inx
        jmp overrun_loop
overrun_done
        rts

; ------------------------------------------------------------ drain_status ---
; Appends this block's status text. Stops storing at 255 bytes and records that
; in bit 2 of R_FLAGS; the DATA_ACC that ends the transaction resets the queue.
drain_status
        ldx R_SLEN
status_loop
        lda CTRL
        and #ST_STAT
        beq status_done
        cpx #$FF
        beq status_full
        lda STATR
        sta STATBUF,x
        inx
        jmp status_loop
status_full
        lda R_FLAGS
        ora #$04
        sta R_FLAGS
status_done
        stx R_SLEN
        rts

; ------------------------------------------------------------ record_block ---
; Stores this block's status byte and the running data total, then advances the
; block index. The totals are cumulative; the host differences them. Blocks past
; the eight the result block has room for are counted but not stored, so a host
; that asks for more than that cannot make this write outside it.
record_block
        ldx zblk
        cpx #$08
        bcs record_count
        lda zstat
        sta R_BSTAT,x
        txa
        asl a
        tay
        lda R_DLEN
        sta R_BLEN,y
        lda R_DLEN+1
        sta R_BLEN+1,y
record_count
        inc zblk
        lda zblk
        sta R_BLOCKS
        rts
