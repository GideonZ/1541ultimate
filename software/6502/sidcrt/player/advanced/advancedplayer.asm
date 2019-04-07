;-----------------------------------------------------------------------
; FILE advancedplayer.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2019 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   This is the advanced SID player that will display a clock with time bar
;   and it will display info about the system's SID chip model and C64 clock
;   frequency. It will also handle keyboard short-cuts to control the SID
;   player.
;   This player is a wrapper around the normal SID player.
;
; Use 64tass version 1.53.1515 or higher to assemble the code
;-----------------------------------------------------------------------

headersize      .word headerEnd - headersize
codeSize        .word codeEnd
playerLoc       .word player + 1                    ; offset of address of player

clockLoc        .word clock.digit + 2               ; offset of address of screen location for clock

songLenLoc1     .word songLengthDigit1 + 2          ; offset of address of screen location for song length
songLenLoc2     .word songLengthDigit2 + 2          ; offset of address of screen location for song length

songNumLoc      .word songDigit + 1                 ; offset of address of screen location for song number

sidModelLoc     .word sidModelWrite + 1             ; offset of address of screen location for system SID model
c64ModelLoc     .word c64ModelWrite + 1             ; offset of address of screen location for system C64 model

songNumSet      .word setSubTune + 1                ; offset of address where the sub tune number must be set

songNum         .word currentSong                   ; offset of address of the current song number
maxSongLoc      .word maxSong                       ; offset of address of the max song number

hdrSpeedFlags   .word speedFlags                    ; offset of address that holds the 4 byte speed flags
hdrSpeedFlag1   .word speedFlag1 + 1                ; offset of address where the address of the current speed flag must be stored
hdrSpeedFlag2   .word speedFlag2 + 1                ; offset of address where the address of the current speed flag must be stored
hdrSpeedFlag3   .word speedFlag3 + 1                ; offset of address where the address of the current speed flag must be stored

fastFwd         .word keyboard.fastForward + 1      ; offset of address where to store the address of the fastforward indicator
pauseKey        .word keyboard.pause + 1            ; offset of address where to store the address of the pause indicator
cantPauseLoc    .word cantPause                     ; offset of address where to store the address of the cantPause indicator

songLenData     .word songLength                    ; offset of songlength data

c64ModelFlag    .word sidC64Model                   ; offset of address of the C64 model specificied in the SID
playerLoopLoc   .word playerLoop + 1                ; offset of address where the address of the player loop must be stored
epCallLoc       .word playerCall + 1                ; offset of address where the extra player is called

spriteLoc       .word timebar.spriteLocation + 1    ; offset of address of the location of the sprite for the time bar

musTuneFlag     .word musTune + 1                   ; offset of flag indicating if the tune is a MUS tune
musColorsLoc    .word musColors                     ; offset of address where the colors are stored when the tune is a MUS file
numColorsLoc    .word numOfColors + 1               ; offset of number of colors to write

sidFxFound      .word sidFxDetected + 1             ; offset of address of flag that indicated if SIDFX was found
sidModelFound   .word sidModel                      ; offset of address of detected SID model
headerEnd

codeStart
                .logical $0000
;---------------------------------------
                jmp extraPlayer
initMain        jsr init
player          jsr $0006     ; init player

loop            jsr extraPlayer

                bit $d011
                bmi +
-               bit $d011
                bpl -
-               bit $d011
                bmi -
                bpl loop
+
-               bit $d011
                bmi -
-               bit $d011
                bpl -
                bmi loop

init            sei
                lda @w *+2
                sta extraPlayLoc        ; store hi-byte of the extra player

                tsx
                stx sp

                lda #0
                sta afterInitDone

                lda maxSong
                jsr math.convertNumToDecDigit
                sta maxSongDigit1
                stx maxSongDigit2
                sty maxSongDigit3

musTune         lda #$00
                beq +
                jsr writeMusColors
+
                jsr clock.initClock
                jsr detectSidModel
                sta sidModel
                jsr displaySysInfo

                jsr fixTimer
                jmp resetVariables

fixTimer        lda sidC64Model
                beq sidPal

                lda palntsc       ; check if palntsc is PAL (0 or 4)
                and #$03
                beq timerOutsideIrq
noFreqFix       rts

sidPal          lda palntsc       ; check if palntsc is PAL (0 or 4)
                and #$03
                beq noFreqFix
timerOutsideIrq
                ; System is NTSC but SID is PAL
                ; or system is PAL but SID is NTSC

                lda #$2c              ; BIT
playerCall      sta @w $0000
                lda #$60              ; RTS
playerLoop      sta @w $0000
                rts

afterInit       jmp timebar.afterInit

extraPlayer     lda afterInitDone
                bne +
                inc afterInitDone
                jsr afterInit

+               lda pauseTune
                bne +
                jsr clock.countClock
                jsr clock.displayClock
                jsr timebar.countTimeBar
+               jmp keyboard.handleKeyboard

selectSubTune   sei
                lda #$00
                sta $d01a
                sta $d418

                lda #$7f
                sta $dc0d
                sta $dd0d
                sta $dc00
                lda #$08
                sta $dc0e
                sta $dd0e
                sta $dc0f
                sta $dd0f

                lda $dc0d
                lda $dd0d
                inc $d019

                jsr resetVariables
                jsr clock.displayClock
                jsr timebar.afterInit

                jsr detectSidModel
                sta sidModel
                jsr displaySysInfo

                lda currentSong
setSubTune      sta @w $0000

                jsr calcSpeedFlag
speedFlag1      sta @w $0000
speedFlag2      sta @w $0000
speedFlag3      sta @w $0000

                ldx sp
                txs         ; reset stack pointer to its original state
                jmp player

; speed flag of tune 32 is also used for tunes 33 - 256
; (old implementation is not supported since current SID collections don't have them anymore)
calcSpeedFlag   cmp #32             ; check if less than 32
                bcc +
                lda #32 - 1         ; select song 32
+               tay
                and #$07
                tax
                tya

                lsr
                lsr
                lsr
                and #$03
                eor #$03
                tay
                lda speedFlags,y
-               dex
                bmi +
                lsr
                bpl -
+               and #$01
                rts

;-------------------------------------------------------------------
resetVariables  jsr clock.resetClock

                lda currentSong
                jsr math.convertNumToDecDigit
                sta curSongDigit1
                stx curSongDigit2
                sty curSongDigit3

                jsr displaySongInfo

                jmp timebar.initTimeBar

displaySongInfo ldx #$00
                ldy #$00
                sty totalSng + 1
-               lda curSongDigit1,x
totalSng        cpy #$00
                bne songDigit
checkSkip       cmp #'0'
                beq +
songDigit       sta $05c0,y
                iny
+               inx
                cpx #$06
                bne +
                sty totalSng + 1
+               cpy #$09
                bne -

displaySongLength
                lda currentSong
                asl
                tax

                lda extraPlayLoc
                adc #>songLength
                sta slDataLoc+2

                inx
                ldy #$03
slDataLoc
-               lda songLength,x
                pha
                and #$0f
                clc
                adc #$30
songLengthDigit1
                sta $07bc,y
                pla
                lsr
                lsr
                lsr
                lsr
                clc
                adc #$30
songLengthDigit2
                sta $07bb,y
                dey
                cpy #$02
                bne +
                lda #':'
                bne songLengthDigit2
+
                dex
                dey
                bpl -
                rts

detectSidModel
sidFxDetected   lda #$00        ; was SIDFX detected?
                beq noSidFx
                lda sidModel    ; return detected SIDFX model
                rts

noSidFx         jmp detection.detectSidModel

displaySysInfo  jsr setBankAllRam

                ldy sidModel
                ldx sidModelIndex,y

                ldy #$00
-               lda sidModelDesc,x
                beq +
sidModelWrite   sta $0570,y
                inx
                iny
                bne -

+               ldx #$00
                lda palntsc
                and #$03
                beq +
                ldx #$07
+
-               lda c64ModelDesc,x
                beq +
c64ModelWrite   sta $0570,y
                inx
                iny
                bne -
+
restoreBank     lda #$37
                sta $01
oldPS           lda #$00
                pha
                plp
                rts

setBankAllRam   php
                pla
                sta oldPS + 1
                sei
                lda $01
                sta restoreBank + 1
                lda #$34
                sta $01
                rts

writeMusColors  ldx #5
                lda #$07
-               sta $d878,x
                dex
                bpl -
                lda #$0f
                sta $d87e

                lda #$80
                sta $aa
                lda #$d8
                sta $ab

                ldx #0
-               ldy #0
-               lda musColors,x
                sta ($aa),y
                inx
                iny
                cpy #32
                bne -

                lda $aa
                adc #$27
                sta $aa
                bcc +
                inc $ab
+
numOfColors     cpx #5*32
                bne --
                rts

;----------------------------------
clock           .binclude 'clock.asm'
keyboard        .binclude 'keyboard.asm'
timebar         .binclude 'timebar.asm'
math            .binclude 'math.asm'
detection       .binclude 'detection.asm'

codeEnd

                .dsection data
palntsc         .byte ?
sidModel        .byte ?   ; 0 = 8580, 1 = 6581, 2 = unknown
sidC64Model     .byte 0   ; 0 = PAL, 1 = NTSC

                .enc 'screen'
c64ModelDesc    .text ' / PAL', 0, ' / NTSC', 0
sidModelDesc    .text '8580', 0, '6581', 0, 'UNKNOWN'  ; not needed to end with zero, since sidModelIndex starts with a zero
                .enc 'none'
sidModelIndex   .byte 0, 5, 10

extraPlayLoc    .byte 0

speedUpFrame    .byte 0
delayFrame      .byte 0

currentSong     .byte 0
maxSong         .byte 0
pauseTune       .byte 0
cantPause       .byte 0

curSongDigit1   .byte 0
curSongDigit2   .byte 0
curSongDigit3   .byte 0
                .byte ' ', '/', ' '
maxSongDigit1   .byte 0
maxSongDigit2   .byte 0
maxSongDigit3   .byte 0
                .byte ' ', ' ', ' ', ' '

speedFlags      .byte 0, 0, 0, 0

songLengthBin   .byte 0, 0
songLengthFrm   .byte 0, 0

afterInitDone   .byte 0

sp              .byte 0  ; used for storing the stackpointer
songLength      .byte $00, $00
musColors       .byte ?

                .here