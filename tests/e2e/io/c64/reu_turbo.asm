; reu_turbo.asm - copy a block out to the REU and back again with the CPU above
; 1 MHz, and check every byte survived the round trip.
;
; This is the E2E form of `software/6502/unsorted/reu_test.tas`, which is the
; program the REU start-of-transfer defect was found and fixed with. The
; sequence under test is the same, and so is the VICE convention that file
; follows: $D7FF ends at $00 for a pass and $02 for a failure, and the border
; is green or red to match. Two things are added for a run on real hardware
; driven over REST:
;
;   - it waits out the CPU and REU slowdown the machine applies for the first
;     seconds after a reset. runners:run_prg resets, loads and starts in one
;     step, so a program that began work immediately would do all of it at
;     1 MHz, which is the one speed at which the defect cannot appear.
;   - it writes a result block into RAM as well, because $D7FF is I/O space and
;     the host reads its verdict back with machine:readmem.
;
; What the sequence tests is that starting a transfer stops the 6510 in the
; same cycle as the write to $DF01. The store that starts the transfer is
; followed straight away by the register writes that set up the next one. At
; 1 MHz the CPU is already halted by the time the next instruction would run.
; Above 1 MHz it is not, unless the core asserts DMA on the command write
; itself, and those instructions then rewrite the base and length registers of
; the transfer still in flight. The bytes that come back are not the bytes that
; went out.
;
; So: no padding, no waiting and no reading of the REU status register between
; the command write and the register writes that follow it. Anything inserted
; there hides the defect.
;
; The source block is screen memory, so the VIC is fetching from it throughout
; and a failure also shows on the picture. The copy comes back to $1400.
;
; Protocol with the host, which reads the result block over machine:readmem:
;   1. READY becomes $A5 once the program is running, before the settle wait
;   2. RUNNING becomes $A5 when the wait is over and the first round begins
;   3. STATUS is 0 while it runs, 1 when every round matched, 2 on a mismatch
;   4. on a mismatch, ITER, ERRADDR, EXPECT and GOT say which byte and which
;      round, so the host reports the failure rather than just its existence
;
; The host side is tests/e2e/io/c64/reu_turbo_test.py, which assembles this with
; the repository's own 64tass and starts it over runners:run_prg.

; ------------------------------------------------------------ REU registers --
reu_status      = $DF00
reu_command     = $DF01
reu_c64base_l   = $DF02
reu_c64base_h   = $DF03
reu_reubase_l   = $DF04
reu_reubase_m   = $DF05
reu_reubase_h   = $DF06
reu_translen_l  = $DF07
reu_translen_h  = $DF08
reu_control     = $DF0A

reu_mode_toreu  = $B0
reu_mode_toc64  = $B1
reu_ctrl_nofix  = $00

; ------------------------------------------------------------- result block --
STATUS   = $C000                ; 0 running, 1 all rounds matched, 2 mismatch
READY    = $C001                ; $A5 once the program is running
ITER     = $C002                ; the round being run, and the one that failed
ERRADDR  = $C003                ; 16 bit address of the first byte that differed
EXPECT   = $C005                ; what that byte should have been
GOT      = $C006                ; what came back from the REU
RUNNING  = $C007                ; $A5 once the settle wait is over and round 0
                                ; has started, so the host times the rounds and
                                ; not the wait

; ------------------------------------------------- the VICE test convention --
VICE_RESULT = $D7FF             ; $00 pass, $02 fail, as reu_test.tas writes it
BORDER      = $D020
GREEN       = $05
RED         = $02

; --------------------------------------------------------------- zero page ---
zptr     = $FB                  ; walks the copy while it is verified
zframes  = $FD                  ; frames left in the settle wait

; -------------------------------------------------------------- the buffers --
SRC      = $0400                ; screen memory: the VIC reads it while we do
DST      = $1400
PAGES    = $04                  ; 1024 bytes, the length both transfers carry

; How long to wait before starting. Whole frames, so it costs the same wall
; clock however fast the CPU is running: 250 frames is 5.0s on PAL and 4.2s on
; NTSC, against the "couple of seconds" the machine holds the CPU and REU down
; for after a reset. Raster line $80 exists on both, and once per frame.
SETTLE_FRAMES = 250
SETTLE_LINE   = $80

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
        sta STATUS
        sta ITER
        sta RUNNING
        lda #$A5
        sta READY

        ; Sit out the post-reset slowdown. Counted in frames rather than in
        ; instructions, which would themselves speed up once it lifted.
        lda #SETTLE_FRAMES
        sta zframes
settle
        lda #SETTLE_LINE
-       cmp $D012
        bne -
-       cmp $D012
        beq -
        dec zframes
        bne settle

        lda #$A5
        sta RUNNING

round
        ; Fill the source with a pattern that changes every round, so a round
        ; that reads back the previous round's bytes fails rather than passing
        ; on data the REU never returned.
        ldx #$00
-       txa
        clc
        adc ITER
        sta SRC,x
        sta SRC+$100,x
        sta SRC+$200,x
        sta SRC+$300,x
        inx
        bne -

        ; Out to REU address 0.
        lda #$00
        sta reu_reubase_l
        sta reu_reubase_m
        sta reu_reubase_h
        sta reu_translen_l
        sta reu_c64base_l
        lda #PAGES
        sta reu_translen_h
        lda #>SRC
        sta reu_c64base_h
        lda #reu_ctrl_nofix
        sta reu_control
        lda #reu_mode_toreu
        sta reu_command

        ; And straight back into a different block, with no instruction in
        ; between. This is the sequence under test: these stores land on the
        ; registers of the transfer above unless it has already stopped the CPU.
        lda #$00
        sta reu_reubase_l
        sta reu_reubase_m
        sta reu_reubase_h
        sta reu_translen_l
        sta reu_c64base_l
        lda #PAGES
        sta reu_translen_h
        lda #>DST
        sta reu_c64base_h
        lda #reu_ctrl_nofix
        sta reu_control
        lda #reu_mode_toc64
        sta reu_command

        ; Verify. The pointer walk costs a few cycles over four unrolled
        ; compares and buys the address of the byte that differed.
        lda #<DST
        sta zptr
        lda #>DST
        sta zptr+1
        ldx #PAGES
verify_page
        ldy #$00
verify_byte
        tya
        clc
        adc ITER
        cmp (zptr),y
        bne mismatch
        iny
        bne verify_byte
        inc zptr+1
        dex
        bne verify_page

        inc ITER
        beq passed              ; wrapped: all 256 rounds matched
        jmp round

passed
        lda #$00
        sta VICE_RESULT
        lda #GREEN
        sta BORDER
        lda #$01
        sta STATUS
        cli
        rts

; The compare left the expected byte in A and the offset within the page in Y.
mismatch
        sta EXPECT
        lda (zptr),y
        sta GOT
        tya
        clc
        adc zptr
        sta ERRADDR
        lda zptr+1
        adc #$00
        sta ERRADDR+1
        lda #$02
        sta VICE_RESULT
        lda #RED
        sta BORDER
        lda #$02
        sta STATUS
        cli
        rts
