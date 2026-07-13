; printer-e2e.asm - Parametrized virtual-printer E2E workload for Ultimate 64/64e.
;
; Assembled with 64tass (tools/64tass or a system-installed 64tass) into a PRG
; that is uploaded and run via POST /v1/runners:run_prg. Runtime parameters are
; POSTed into RAM at PARAM_BASE (via /v1/machine:writemem) by printer-e2e.py
; *before* the PRG is started. Progress/result are polled from STATUS_BASE via
; GET /v1/machine:readmem.
;
; Parameter block (PARAM_BASE = $C010), written by the host before running:
;   $C010  emulation   1 = Epson FX-80/JX-80, 2 = Commodore MPS
;   $C011  mode        1 = bitmap, 2 = text
;   $C012  rows        bitmap lines or text rows per page (1-255)
;   $C013  pages       number of pages to print and eject (1-9)
;   $C014  bus id      IEC device number to OPEN (4 or 5)
;   $C015  bim repeats bitmap mode only: seed-pattern repeats per row (1-255);
;                      16 bytes/repeat, ignored in text mode. Controls row
;                      width - a large value (e.g. 120) spans the full
;                      printable page width for a full-page bitmap test.
;   $C016  flags: bit 0 emits the issue #717 BASIC byte shape without an
;                      explicit form feed; Flush/Eject saves the final page.
;
; Status block (STATUS_BASE = $C000), maintained by this program:
;   $C000  magic0 = $55
;   $C001  magic1 = $36
;   $C002  phase (see PHASE_* below)
;   $C003  emulation echoed from parameter block
;   $C004  mode echoed from parameter block
;   $C005  current page (1-based)
;   $C006  current row (1-based)
;   $C007  last READST value
;   $C008  final status (0 = still running, $7F = complete, $80 = failed)
;   $C009  last KERNAL error code (X register after a failed OPEN)
;   $C00A  heartbeat low byte
;   $C00B  heartbeat high byte

STATUS_BASE = $C000
PARAM_BASE  = $C010

PH_NOT_STARTED  = $00
PH_OPENING      = $10
PH_OPENED       = $11
PH_PRINTING     = $20
PH_FORMFEED     = $30
PH_CLOSING      = $40
PH_CLOSED       = $41
PH_COMPLETE     = $7F
PH_FAILED       = $80

SETLFS = $ffba
SETNAM = $ffbd
OPEN   = $ffc0
CHKOUT = $ffc9
CHROUT = $ffd2
CLRCHN = $ffcc
CLOSE  = $ffc3
READST = $ffb7

; This program deliberately avoids zero page entirely for its own state
; (loop counters below live in plain static storage, and printstr uses
; self-modifying code instead of zero-page indirect addressing). A page/row
; counter placed in supposedly-"free" zero page ($F8-$FA) was corrupted
; partway through a long multi-page CHROUT session, silently truncating
; later pages - avoiding zero page sidesteps needing to know exactly which
; locations the IEC/KERNAL I/O path touches internally.

        * = $0801
        .word next, 10
        .byte $9e
        .text "2064"
        .byte 0
next    .word 0

        * = $0810

start:
        ; ---- initialise status block ----
        lda #$55
        sta STATUS_BASE
        lda #$36
        sta STATUS_BASE+1
        lda #PH_NOT_STARTED
        sta STATUS_BASE+2
        lda PARAM_BASE          ; emulation
        sta STATUS_BASE+3
        lda PARAM_BASE+1        ; mode
        sta STATUS_BASE+4
        lda #0
        sta STATUS_BASE+5
        sta STATUS_BASE+6
        sta STATUS_BASE+7
        sta STATUS_BASE+8
        sta STATUS_BASE+9
        sta STATUS_BASE+10
        sta STATUS_BASE+11

        ; ---- open printer channel ----
        lda #PH_OPENING
        sta STATUS_BASE+2

        lda #1                  ; logical file number 1
        ldx PARAM_BASE+4        ; bus id (device number)
        ldy #0                  ; secondary address 0
        jsr SETLFS
        lda #0
        jsr SETNAM
        jsr OPEN
        bcc open_ok
        lda #PH_FAILED
        sta STATUS_BASE+2
        sta STATUS_BASE+8
        stx STATUS_BASE+9
        jmp finished

open_ok:
        ldx #1
        jsr CHKOUT
        bcc chkout_ok
        lda #PH_FAILED
        sta STATUS_BASE+2
        sta STATUS_BASE+8
        jmp close_and_finish

chkout_ok:
        lda #PH_OPENED
        sta STATUS_BASE+2

        lda PARAM_BASE+6
        and #$01
        bne init_done

        ; ---- emulation-specific job init ----
        lda PARAM_BASE
        cmp #1
        bne init_done           ; only Epson needs an explicit init sequence
        lda #$1b
        jsr CHROUT
        lda #$40                ; ESC @ (reset to power-on defaults)
        jsr CHROUT

        lda PARAM_BASE+1        ; mode
        cmp #1                   ; bitmap only: use tight 8-dot line spacing so
        bne init_done            ; consecutive BIM rows form a contiguous band;
        lda #$1b                 ; leave the default (taller) interline alone
        jsr CHROUT               ; for text mode so NLQ rows don't overlap.
        lda #$41                ; ESC A
        jsr CHROUT
        lda #$08                ; 8/72" line spacing (8-dot bitmap rows)
        jsr CHROUT
init_done:

        ; ---- text mode only: switch to NLQ + bold + double-strike so the
        ; ---- printed characters are dense/blocky enough for OCR to read
        ; ---- back reliably (the default draft-quality dot pattern is too
        ; ---- sparse for OCR, even after image preprocessing).
        lda PARAM_BASE+6
        and #$01
        bne text_init_done
        lda PARAM_BASE+1        ; mode
        cmp #2
        bne text_init_done

        lda PARAM_BASE           ; emulation
        cmp #1
        bne text_init_cbm

        lda #$1b                 ; Epson: ESC x 1 (NLQ on)
        jsr CHROUT
        lda #$78
        jsr CHROUT
        lda #$01
        jsr CHROUT
        lda #$1b                 ; ESC E (emphasized/bold on)
        jsr CHROUT
        lda #$45
        jsr CHROUT
        lda #$1b                 ; ESC G (double-strike on)
        jsr CHROUT
        lda #$47
        jsr CHROUT
        jmp text_init_done

text_init_cbm:
        lda #$1f                 ; Commodore: NLQ on
        jsr CHROUT
        lda #$1b                 ; ESC E (emphasized/bold on)
        jsr CHROUT
        lda #$45
        jsr CHROUT
        lda #$1b                 ; ESC G (double-strike on)
        jsr CHROUT
        lda #$47
        jsr CHROUT

text_init_done:

        lda #PH_PRINTING
        sta STATUS_BASE+2

        lda #1
        sta pagenum
        lda PARAM_BASE+3        ; pages
        sta pageleft

page_loop:
        lda pagenum
        sta STATUS_BASE+5

        lda #1
        sta rownum
        lda PARAM_BASE+2        ; rows
        sta rowleft

row_loop:
        lda rownum
        sta STATUS_BASE+6

        lda PARAM_BASE+6
        and #$01
        bne do_issue_717_row
        lda PARAM_BASE+1        ; mode
        cmp #1
        beq do_bitmap_row
        jsr print_text_row
        jmp row_done
do_issue_717_row:
        jsr print_issue_717_row
        jmp row_done
do_bitmap_row:
        jsr print_bitmap_row

row_done:
        jsr READST
        sta STATUS_BASE+7

        inc STATUS_BASE+10
        bne row_heartbeat_done
        inc STATUS_BASE+11
row_heartbeat_done:

        inc rownum
        dec rowleft
        bne row_loop

        lda PARAM_BASE+6
        and #$01
        bne issue_717_finished

        ; ---- eject page ----
        lda #PH_FORMFEED
        sta STATUS_BASE+2
        lda #$0c
        jsr CHROUT
        jsr READST
        sta STATUS_BASE+7

        inc pagenum
        dec pageleft
        bne page_loop

        lda #PH_COMPLETE
        sta STATUS_BASE+2
        sta STATUS_BASE+8

        jmp close_and_finish

issue_717_finished:
        lda #PH_COMPLETE
        sta STATUS_BASE+2
        sta STATUS_BASE+8
        rts

close_and_finish:
        lda #PH_CLOSING
        sta STATUS_BASE+2
        jsr CLRCHN
        lda #1
        jsr CLOSE
        jsr READST
        sta STATUS_BASE+7
        lda #PH_CLOSED
        sta STATUS_BASE+2

finished:
        rts

; ---------------------------------------------------------------------------
; print_text_row: emit one deterministic text row for the current emulation.
;   "U64PRN <EPSON|COMMODORE> TEXT PAGE=NNN ROW=NNN ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789"
; ---------------------------------------------------------------------------
print_text_row:
        lda #<str_u64prn
        sta ps_lda+1
        lda #>str_u64prn
        sta ps_lda+2
        jsr printstr

        lda PARAM_BASE
        cmp #1
        bne pt_commodore_tag
        lda #<str_epson
        sta ps_lda+1
        lda #>str_epson
        sta ps_lda+2
        jmp pt_tag_done
pt_commodore_tag:
        lda #<str_commodore
        sta ps_lda+1
        lda #>str_commodore
        sta ps_lda+2
pt_tag_done:
        jsr printstr

        lda #<str_texttag
        sta ps_lda+1
        lda #>str_texttag
        sta ps_lda+2
        jsr printstr

        lda pagenum
        jsr print3digit

        lda #<str_rowtag
        sta ps_lda+1
        lda #>str_rowtag
        sta ps_lda+2
        jsr printstr

        lda rownum
        jsr print3digit

        lda #<str_suffix
        sta ps_lda+1
        lda #>str_suffix
        sta ps_lda+2
        jsr printstr

        ; ---- row terminator ----
        lda PARAM_BASE
        cmp #1
        bne pt_cbm_eol
        lda #$0a                ; Epson: LF then CR
        jsr CHROUT
        lda #$0d
        jsr CHROUT
        rts

; ---------------------------------------------------------------------------
; print_issue_717_row: emit the byte shape of the BASIC reproducer:
;   PRINT#1,"line no "; i
;   PRINT#1, CHR$(13)
; The decimal value is immaterial to the printer parser. The normal printer
; line ending advances the Epson raster, followed by the explicit CR. This
; routine intentionally sends no form feed.
; ---------------------------------------------------------------------------
print_issue_717_row:
        lda #<str_issue_717
        sta ps_lda+1
        lda #>str_issue_717
        sta ps_lda+2
        jsr printstr

        lda rownum
        jsr print3digit
        lda #$0a
        jsr CHROUT
        lda #$0d
        jsr CHROUT
        lda #$0d
        jsr CHROUT
        rts
pt_cbm_eol:
        lda #$0d                ; Commodore: CR (advances line, resets column)
        jsr CHROUT
        rts

; ---------------------------------------------------------------------------
; print_bitmap_row: emit one deterministic bit-image row for the current
; emulation, repeating the 16-byte seed pattern PARAM_BASE+5 ("bim repeats")
; times to form a visible horizontal band (or, with a large repeat count,
; a row spanning the full printable page width).
; ---------------------------------------------------------------------------
print_bitmap_row:
        ; bim_len (16-bit) = bim_repeats * 16, used as the Epson ESC Z n/m
        ; byte count. bim_repeats fits in a byte, so bim_len always fits in
        ; 16 bits (max 255*16 = 4080).
        lda PARAM_BASE+5
        sta bim_len_lo
        lda #0
        sta bim_len_hi
        ldx #4
bim_len_shift:
        asl bim_len_lo
        rol bim_len_hi
        dex
        bne bim_len_shift

        lda PARAM_BASE
        cmp #1
        beq pb_epson
        jmp pb_commodore

pb_epson:
        lda #$1b
        jsr CHROUT
        lda #$5a                 ; ESC Z (quadruple density, ~240dpi)
        jsr CHROUT
        lda bim_len_lo            ; n
        jsr CHROUT
        lda bim_len_hi            ; m
        jsr CHROUT

        ldx PARAM_BASE+5
pb_epson_reps:
        stx pb_rep_save
        ldy #0
pb_epson_bytes:
        lda epson_pattern,y
        jsr CHROUT
        iny
        cpy #16
        bne pb_epson_bytes
        ldx pb_rep_save
        dex
        bne pb_epson_reps

        lda #$0a
        jsr CHROUT
        lda #$0d
        jsr CHROUT
        rts

pb_commodore:
        lda #$08                 ; enter bit image mode
        jsr CHROUT

        ldx PARAM_BASE+5
pb_cbm_reps:
        stx pb_rep_save
        ldy #0
pb_cbm_bytes:
        lda commodore_pattern,y
        jsr CHROUT
        iny
        cpy #16
        bne pb_cbm_bytes
        ldx pb_rep_save
        dex
        bne pb_cbm_reps

        lda #$0f                 ; exit bit image mode
        jsr CHROUT
        lda #$0d
        jsr CHROUT
        rts

pb_rep_save: .byte 0
bim_len_lo:  .byte 0
bim_len_hi:  .byte 0

; ---------------------------------------------------------------------------
; Loop counters (plain static storage, not zero page - see the comment by
; strptr above).
; ---------------------------------------------------------------------------
pagenum:  .byte 0   ; current page number, 1-based (for printing/status)
pageleft: .byte 0   ; pages remaining (down counter)
rownum:   .byte 0   ; current row number, 1-based (for printing/status)
rowleft:  .byte 0   ; rows remaining in current page (down counter)
digit10:  .byte 0   ; scratch for print3digit

; ---------------------------------------------------------------------------
; printstr: print the null-terminated string whose address the caller has
; poked into ps_lda+1/+2 (self-modifying code, not zero-page indirect
; addressing - see the comment by the loop counters above for why).
; ---------------------------------------------------------------------------
printstr:
        ldy #0
ps_loop:
ps_lda: lda $ffff,y
        beq ps_done
        jsr CHROUT
        iny
        bne ps_loop
ps_done:
        rts

; ---------------------------------------------------------------------------
; print3digit: print A (0-255) as three zero-padded decimal digits.
; ---------------------------------------------------------------------------
print3digit:
        ldx #0
p3_hundreds:
        cmp #100
        bcc p3_hundreds_done
        sbc #100
        inx
        jmp p3_hundreds
p3_hundreds_done:
        sta digit10              ; save remainder (0-99)
        txa
        clc
        adc #'0'
        jsr CHROUT

        lda digit10
        ldx #0
p3_tens:
        cmp #10
        bcc p3_tens_done
        sec
        sbc #10
        inx
        jmp p3_tens
p3_tens_done:
        sta digit10               ; save remainder (0-9)
        txa
        clc
        adc #'0'
        jsr CHROUT

        lda digit10
        clc
        adc #'0'
        jsr CHROUT
        rts

; ---------------------------------------------------------------------------
; Deterministic seed patterns
; ---------------------------------------------------------------------------
commodore_pattern:
        .byte $88,$94,$a2,$c1,$a2,$94,$88,$88
        .byte $9c,$ba,$ff,$ba,$9c,$88,$eb,$88

epson_pattern:
        .byte $3c,$42,$81,$81,$81,$42,$3c,$18
        .byte $3c,$7e,$ff,$7e,$3c,$18,$eb,$18

str_u64prn:     .text "U64PRN "
                .byte 0
str_epson:      .text "EPSON"
                .byte 0
str_commodore:  .text "COMMODORE"
                .byte 0
str_texttag:    .text " TEXT PAGE="
                .byte 0
str_rowtag:     .text " ROW="
                .byte 0
str_suffix:     .text " ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789"
                .byte 0
str_issue_717:  .text "line no "
                .byte 0
