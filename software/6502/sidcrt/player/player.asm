;-----------------------------------------------------------------------
; FILE player.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   This is the SID player which is built-in the Ultimate devices. The
;   player is also used by externals players, such as ACID64, to play SID
;   tunes via the socket interface.
;
; Use 64tass version 1.53.1515 or higher to assemble the code
;-----------------------------------------------------------------------

headerSize      .word headerEnd - headerSize
offSongNr       .word songNr + 1        ; offset of the song to be played
offBasSongNr    .word basicSongNr + 1   ; offset of the song to be played when it is a BASIC tune
offInit         .word initAddress + 1   ; offset of address of init routine
offPlay         .word playAddress + 1   ; offset of address of play routine

offInitBank     .word initBank + 1      ; offset of value of address $0001 to be set before init call
offInitAfter    .word initBankAfter     ; offset of instruction to be nopped out when play address is zero
offPlayBank     .word playBank + 1      ; offset of value of address $0001 to be set before play call

offD018         .word d018Val + 1       ; offset of value of address $D018 to be set
offDD00         .word dd00Val + 1       ; offset of value of address $DD00 to be set
offSpeed1       .word speedFlag + 1     ; offset of value for speedFlag for the current sub tune
offSpeed2       .word speedFlag2 + 1    ; offset of value for speedFlag for the current sub tune
offSpeed3       .word speedFlag3 + 1    ; offset of value for speedFlag for the current sub tune

offPlayNull1    .word playIsNull + 1    ; offset of value to indicate the play address is null
offPlayNull2    .word playIsNull2 + 1   ; offset of value to indicate the play address is null
offClock        .word clock + 1         ; offset of value for the clock setting: 0 = PAL, 1 = NTSC

offHiBrk        .word hiBrk + 1         ; hi byte of location player for BRK handling routine
offHiIrq        .word hiIrq + 1         ; hi byte of location player for IRQ handling routine

offRsid         .word rsidTune + 1      ; offset of flag indicating the SID type (PSID / RSID)
offBasic        .word basicTune + 1     ; offset of flag indicating if the tune is a BASIC tune

offLoEnd        .word loEndAddress + 1  ; offset of lo byte of end address of SID file (required for BASIC tunes)
offhiEnd        .word hiEndAddress + 1  ; offset of hi byte of end address of SID file (required for BASIC tunes)

offCiaFix       .word ciaFixSet + 2     ; offset of hi byte of address to set byte to fix CIA/IRQ setting

offReloc1       .word reloc1 + 2        ; offset of hi byte of jmp instruction which should be relocated

offCiaLU1       .word ciaLookup1 + 2    ; offset of hi byte of CIA lookup table
offCiaLU2       .word ciaLookup2 + 2    ; offset of hi byte of CIA lookup table

offBasicEnd     .word basicEnd + 1      ; offset of hi byte of player location when BASIC ends
offHiPlayer     .word hiPlayer + 2      ; offset of hi byte of player

offCiaFix1      .word ciaSpeedFix1 + 2  ; offset of hi byte of code to fix CIA speed
offCiaFix2      .word ciaSpeedFix2 + 2  ; offset of hi byte of code to fix CIA speed

offPrgFile      .word prgFile + 1       ; offset of flag indicating if the tune is a PRG file

offExtraPlay1   .word extraPlayer + 0   ; offset of JSR instruction for calling extra player (NOP out when there is no extra player)
offExtraPlay2   .word extraPlayer2 + 0  ; offset of JSR instruction for calling extra player (NOP out when there is no extra player)

offPlayLoop     .word playerLoop + 0    ; offset of player loop which can be disabled to write $60

offFastForw     .word fastForward + 1   ; offset of flag indicating the player should fast forward the tune (0 = normal, other value is fast forward)

offNumLines     .word numOfLines + 1    ; offset of value of the number of line where the colors should be set for
headerEnd
                .logical $0000
playRoutine

hiPlayer        jsr player
playerLoop      clc
                bcc *

player          sei
                lda #$00
                sta $d020
                sta $d021

                ldy #$50
                lda #$0f        ; 2nd, 3rd, 2nd last and last line is light grey
-               sta $d828 - 1,y
                sta $db98 - 1,y
                dey
                bne -

                ldy #$28
-               lda #$01        ; first line is white
                sta $d800 - 1,y
                lda #$00        ; line with sprite for timebar is black
                sta $db70 - 1,y
                dey
                bne -

                lda #$48
                sta $aa
                lda #$db
                sta $ab

numOfLines      ldx #20         ; write screen colors for 21 lines
-               ldy #$27
                lda #$0f
-               sta ($aa),y
                cpy #$06
                bne +
                lda #$07        ; labels are yellow
+               dey
                bpl -

                lda $aa
                sbc #$27
                sta $aa
                bcs +
                dec $ab
+               dex
                bne --

                lda #$1b
                sta $d011

d018Val         lda #$00
                sta $d018
dd00Val         lda #$00
                sta $dd00
                lda #$c8
                sta $d016

                lda #$ea
                sta $0328           ; disable runstop/restore

                lda #$f7
                sta $d1
                lda #$03
                sta $d2             ; set screen line address to offset $03F7 to avoid blinking cursor on screen for some tunes

rsidTune        lda #$00
                beq psidTune
                cli                 ; for RSID clear interrupt before init is called. Needed for tunes like /MUSICIANS/P/PVCF/Centric_end_sequence.sid

loEndAddress    lda #$00
hiEndAddress    ldx #$00
                sta $2d
                stx $2e
                sta $2f
                stx $30
                sta $31
                stx $32
                sta $ae
                stx $af

basicTune       lda #$00
                beq +
                ; it's a BASIC tune

basicSongNr     lda #$00
                sta $030c           ;set song to play in accumulator, x-register and y-register of BASIC
                sta $030d
                sta $030e

                lda $0803
                sta $39
                lda $0804
                sta $3a

prgFile         lda #$00
                bne +

                lda #<playerLoop
                sta $0300
basicEnd        lda #$00
                sta $0301

+
reloc1          jmp playerStart   ; skip psid init since it is an RSID tune

psidTune
                lda #<irqbrk
                sta $0316
hiBrk           lda #>irqbrk
                sta $0317

                ; clear zero page
                ldx #$fe
                lda #$00
-               sta $01,x
                dex
                bne -
noZeroPageFill

detectPalNtsc
-               lda $d012
-               cmp $d012
                beq -
                bmi --
                and #$03
                eor #$03
                bne NTSC
                ; check for pal / drean pal-n
                tax
-               inx
                ldy #$2c
                cpy $d012
                bne -
                inx
                bmi +
                lda #$04
+
; 00 = PAL (312 raster lines, 63 cycles per line)
; 01 = NTSC (263 raster lines, 65 cycles per line)
; 02 = NTSC (262 raster lines, 64 cycles per line, old VIC with bug)
; 04 = PAL Drean (312 raster lines, 65 cycles per line)

; speedflag  machine clock  sid clock  timer
;         1            PAL        PAL  PAL60
;         1            PAL       NTSC  PAL60
;         0            PAL        PAL  PAL50
;         0            PAL       NTSC  PAL60
;         1           NTSC        PAL  NTSC60
;         1           NTSC       NTSC  NTSC60
;         0           NTSC        PAL  NTSC50
;         0           NTSC       NTSC  NTSC60
; to summarize:
; Speed flag is set, then always default timer to 60Hz PAL / NTSC
; only play on 50Hz PAL / NTSC when SID clock is PAL and speedflag is not set

PAL             clc
                adc #$08
                ; 0 -> 8
                ; 4 -> 12
                bpl +

NTSC            asl
                and #$04
                ; 1 -> 0
                ; 2 -> 4

+
speedFlag       ldx #$00            ; speed flag set on PAL machine. 0 = not set, 1 = set
                bne speed60Hz
                ; speed flag not set
clock           ldy #$00            ; clock flag PAL on PAL machine. 0 = PAL, 1 = NTSC
                bne speed60Hz
                ; play on 50Hz
                clc
                adc #$02
speed60Hz       tax
ciaLookup1      lda @w ciaValues,x
                sta $dc04
ciaSpeedFix1    sta @w loSpeed + 1
ciaLookup2      lda @w ciaValues + 1,x
                sta $dc05
ciaSpeedFix2    sta @w hiSpeed + 1

playIsNull      lda #$00
                beq installIrq

speedFlag2      ldx #$00
                bne enableCia
                stx $dc0e
                stx $d012
                inx
                stx $d01a
                lda #$7f
                sta $dc0d
                lda $dc0d
                dex
                beq endIrqInit

installIrq      ldy #<irqciaFFFE
                lda #<irqcia0314
hiIrq           ldx #>irqcia0314
installIrqVec   sta $0314
                stx $0315
                sty $fffe
                stx $ffff

                ldx #$01
enableCia       stx $dc0e           ; start Timer A, continuous
                lda #$81            ; enable CIA A, Timer A
                sta $dc0d
endIrqInit

ciaFixSet       stx @w ciaFix + 1

playerStart     lda #$00
                sta $aa
                sta $ab

                ; init SID chip
                ; write first $ff for tunes like /MUSICIANS/T/Tel_Jeroen/Cybernoid.sid and /MUSICIANS/B/Bjerregaard_Johannes/Stormlord_V2.sid
                lda #$ff
resetSidLoop    ldx #$17
-               sta $d400,x
                dex
                bpl -
                tax
                bpl +
                lda #$08
                bpl resetSidLoop
+
-               bit $d011
                bpl -
-               bit $d011
                bmi -
                eor #$08
                beq resetSidLoop

                lda #$0f
                sta $d418

initBank        lda #$00
                sta $01

songNr          lda #$00
                tax
                tay
initAddress     jsr $0000           ; call init routine of player

                ; Don't set bank when play address is zero
                lda #$37
initBankAfter   sta $01

                ; fix enable CIA/VBI when init routine changes this (needed for some tunes from Deenen/JT)
playIsNull2     lda #$00
                bne +
ciaFix          lda #$00
                sta $dc0e
                eor #$01
                sta $d01a
                lda $dc0d
                inc $d019
+               cli
                rts

.page
irqciaFFFE      pha
                txa
                pha
                tya
                pha

irqcia0314      lda $01
.endp
                pha
playBank        lda #$00
                sta $01
playAddress     jsr $0000
                pla
                sta $01

fastForward     lda #$00
                beq +
                inc $d020
extraPlayer2    jsr $0000           ; call to extra player for e.g. clock and sub tune selection
                clc
                bcc irqcia0314
+               sta $d020

extraPlayer     jsr $0000           ; call to extra player for e.g. clock and sub tune selection

speedFlag3      lda #$00
                bne +

; some tunes like /DEMOS/G-L/In_the_South_African.sid and /DEMOS/G-L/In_This_Moment.sid are changing $dc04/05 for no reason
loSpeed         lda #$00            ; force palSpeed / ntscSpeed depending on $02a6 value and speed flag
                sta $dc04
hiSpeed         lda #$00
                sta $dc05
+
                inc $d019
                lda $dc0d
endIrq          pla                 ; restor YR, XR and AC
                tay
                pla
                tax
                pla
                rti

irqbrk          tsx                 ; interpreter BRK as RTS with AC, XR, YR and processor status register restored
                inx
                inx
                txs
                ldy #$03
                inc $0105,x         ; increase return address by one to simulate the RTS behavior
-               lda $0102,x         ; copy processor status, AC, XR, YR register values on the stack to new location
                sta $0104,x
                dex
                dey
                bpl -
                bmi endIrq

ciaValues       .word $42c6         ; NTSC 60Hz (NTSC speed (263 * 65 - 1))
                .word $5021         ; NTSC 50Hz (PAL speed (263 * 65 * 60 / 50 - 1))
                .word $417f         ; OLD NTSC 60Hz (NTSC speed (262 * 64 - 1))
                .word $4e98         ; OLD NTSC 50Hz (PAL speed (262 * 64 * 60 / 50 - 1))
                .word $3ffb         ; PAL 60Hz (312 * 63 * 50 / 60 - 1)
                .word $4cc7         ; PAL 50Hz (312 * 63 - 1)
                .word $4203         ; PAL DREAN 60Hz (312 * 65 * 50 / 60 - 1)
                .word $4f37         ; PAL DREAN 50Hz (312 * 65 - 1)

.here
