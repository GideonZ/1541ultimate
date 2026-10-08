;-----------------------------------------------------------------------
; FILE sidcommon.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2019 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Common routines and data used by the SID and MUS cartridges.
;-----------------------------------------------------------------------

; CONSTANTS
INCLUDE_RUNSTOP = 1

SID_MODE = $aa
DMA_MODE = $ab

OFFSET_SYSTEM_SCREEN_LOCATION = $ec       ; location at screen + $0300 -> $03ec
OFFSET_SONG_SCREEN_LOCATION = $ee         ; location at screen + $0300 -> $03ee
OFFSET_SPEED_SCREEN_LOCATION = $f0        ; location at screen + $0300 -> $03f0

SYSTEM_SCREEN_LOCATION = $b4
SONG_SCREEN_LOCATION = $b5

; ZERO PAGE ADDRESSES
CURRENT_LINE = $a0
CANT_PAUSE = $a0

DD00_VALUE = $a3
D018_VALUE = $a4

EXTRA_PLAYER_SIZE = $b6                   ; size of the advanced player (including song lengths data)
EXTRA_PLAYER_LOCATION = $b7               ; hi-byte of the advanced player address
PLAYER_LOCATION = $b8                     ; hi-byte of the player address
CHARROM_LOCATION = $b9                    ; hi-byte of the character ROM address where value $10 is the default CHARROM

TEMP = $bc
SID_MODEL = $48
C64_CLOCK = $49
SIDFX_DETECTED = $4a

SID_HEADER_LO = $fa                       ; lo-byte of sid header address
SID_HEADER_HI = $fb                       ; hi-byte of sid header address
SONG_TO_PLAY = $fc                        ; song to play where value 0 is song 1 and $ff is song 256
SCREEN_LOCATION = $fd                     ; hi-byte of screen location

ZERO_PAGE_ADDRESSES_MAIN = [
    CURRENT_LINE,
    DD00_VALUE,
    D018_VALUE,
    $a5, $a6, $a7, $a8, $a9,

    EXTRA_PLAYER_SIZE,
    EXTRA_PLAYER_LOCATION,
    PLAYER_LOCATION,
    CHARROM_LOCATION,
    TEMP,
    SID_MODEL,
    C64_CLOCK,
    SIDFX_DETECTED,

    $f7, $f8,

    SID_HEADER_LO,
    SID_HEADER_HI,
    SONG_TO_PLAY,
    SCREEN_LOCATION,
    $fe
]

cleanupMemory   ldy #$7f            ; clean SID header
                lda #$00
-               sta (SID_HEADER_LO),y
                dey
                bpl -

                lda #$20
                sta $ff

                lda #<cleanUpRoutine
                ldx #>cleanUpRoutine
                ldy #cleanUpRoutineEnd - cleanUpRoutine
runAt0100       jsr copyTo0100
                jmp $0100

copyTo0100      sta $aa
                stx $ab
-               lda ($aa),y
                sta $0100,y
                dey
                bpl -
                rts

cleanUpRoutine
                .logical $0100
                ldx #$00
                ldy #$00
-               lda zpAddressesUsed,x
                beq +
                sta zeroPageWrite + 1
zeroPageWrite   sty $00
                inx
                jmp -
+               rts
                .here
cleanUpRoutineEnd

calculateExtraPlayerSize
                ; extra player size is (extraPlayerEnd - extraPlayer + 1 + numberOfSongs * 2) >> 8
                ldy #$0e            ; get number of songs
                jsr readHeader
                sta $aa
                ldy #$0f            ; get number of songs
                jsr readHeader
                sta $ab

                lda $aa
                asl
                sta $aa
                lda $ab
                rol
                sta $ab

                lda $aa
                clc
                adc #<extraPlayerEnd
                sta $aa
                lda $ab
                adc #>extraPlayerEnd
                sta $ab

                lda $aa
                sec
                sbc #<extraPlayer
                sta $aa
                lda $ab
                sbc #>extraPlayer
                sta $ab

                inc $aa
                bcc +
                inc $ab
+
                lda $ab
                sta EXTRA_PLAYER_SIZE
                rts

writeSidVolAddr pha
                asl
                asl
                asl
                asl
                ora #$18
                jsr setValue
                pla
                lsr
                lsr
                lsr
                lsr
                ora #$d0
                jmp writeNextAddress

getScreenLocationLastHi
                lda SCREEN_LOCATION
                clc
                adc #$03
                rts

getVariableByte jsr setScreenLocPointer
                jmp readScreen

writeAtPlayerLocation
                clc
                adc PLAYER_LOCATION
writeNextAddress
                iny
                jmp writeAddress

getVariableWord jsr getVariableByte
                tax
                iny
                jmp readScreen

setScreenLocPointer
                lda #$00
                sta $b0
                jsr getScreenLocationLastHi
                sta $b1
                rts

setVariableByte pha
                jsr setScreenLocPointer
                pla
                jmp writeScreen

setVariableWord jsr setVariableByte
                txa
                iny
                jmp writeScreen

isPlayZero      jsr getPlayLo
                bne +
                jsr getPlayHi
+               rts

enableExtraPlayerCalls
                jsr isPlayZero
                beq noExtraPlayWithFF
+
                lda #$20            ; JSR
                ldy player.offExtraPlay1
                ldx player.offExtraPlay1 + 1
                jsr setValue
                lda #$00            ; lo-byte of extra player
                jsr writeNextAddress
                lda EXTRA_PLAYER_LOCATION
                jsr writeNextAddress

extraPlayForFF  lda #$20            ; JSR
                ldy player.offExtraPlay2
                ldx player.offExtraPlay2 + 1
                jsr setValue
                lda #$00            ; lo-byte of extra player
                jsr writeNextAddress
                lda EXTRA_PLAYER_LOCATION
                jsr writeNextAddress

                lda #$4c            ; JMP
                ldy player.offExtraPlay3
                ldx player.offExtraPlay3 + 1
                jsr setValue
                lda #<extraPlayer.initMain      ; lo-byte of extra player init routine
                jsr writeNextAddress
                lda EXTRA_PLAYER_LOCATION
                jmp writeNextAddress

noExtraPlayWithFF
                jsr isPlayZero
                bne +

                lda #$01
                sta CANT_PAUSE      ; can't pause tune when play address is zero since it runs in its own IRQ

+               lda EXTRA_PLAYER_LOCATION
                beq noExtraPlayer

                jsr isBasic
                cmp #$01            ; when BASIC tune then always play song in an endless loop and don't allow subtune changes via keyboard
                beq playInLoop

                lda SCREEN_LOCATION ; read screen hi-byte
                and #$f0
                cmp #$d0            ; when screen is located at $Dxxx then don't allow extra player since screen cannot be updated
                beq playInLoop

                ; remove loop so that extra player is called for subtune selection via keyboard

                lda #$60
                ldy player.offPlayLoop
                ldx player.offPlayLoop + 1
                jsr setValue
playInLoop
                lda #$ea            ; NOP
                ldy player.offExtraPlay1
                ldx player.offExtraPlay1 + 1
                jsr setValue
                jsr writeNextAddress
                jsr writeNextAddress
                jmp extraPlayForFF

noExtraPlayer   lda #$ea            ; NOP
                ldy player.offExtraPlay1
                ldx player.offExtraPlay1 + 1
                jsr setValue
                jsr writeNextAddress
                jsr writeNextAddress

                ldy player.offExtraPlay2
                ldx player.offExtraPlay2 + 1
                jsr setValue
                jsr writeNextAddress
                jmp writeNextAddress

; setValueAt
;   input:
;   - AC: the value
;   - XR: offset in the advanced player's header of the word that holds the offset to write the value to
setValueAt      ldy extraPlayer,x
                pha
                lda extraPlayer + 1,x
                tax
                pla

setValue        pha
                tya
                sta $aa
                txa
                clc
                adc relocator.BASE_ADDRESS
                sta $ab
                pla
                ldy #$00
                jmp writeAddress

isRsid          ldy #$00
                jsr readHeader
                ldy #$01
                cmp #'R'            ; check if RSID file
                beq +
                ldy #$00
+               tya
                cmp #$01
                rts

isBasic         ldy #$77
                jsr readHeader
                and #$02            ; is BASIC tune?
                lsr
                cmp #$01
                rts

setSpeedFlags   ldy player.offSpeed1
                ldx player.offSpeed1 + 1
                jsr setValue
                ldy player.offSpeed2
                ldx player.offSpeed2 + 1
                jsr setValue
                ldy player.offSpeed3
                ldx player.offSpeed3 + 1
                jmp setValue

copyPlayer      lda #>player
                sta $ab
                lda #<player
                sta $aa

                ldx #$02      ; size player in blocks of $0100
                ldy PLAYER_LOCATION
                lda player.headerSize
copyPlayerLoop  clc
                adc $aa
                sta $aa
                bcc +
                inc $ab
+
                sty $ad

                ldy #$00
                sty $ac
-               lda ($aa),y
                sta ($ac),y
                iny
                bne -
                inc $ab
                inc $ad
                dex
                bne -
                rts

copyExtraPlayer lda #>extraPlayer
                sta $ab
                lda #<extraPlayer
                sta $aa

                ldx #(extraPlayerEnd - extraPlayer) / 256 + 1      ; size player in blocks of $0100
                ldy EXTRA_PLAYER_LOCATION
                lda extraPlayer.headerSize
                jmp copyPlayerLoop

prepareSidHeader
                ; Get real load address
                ldy #$08            ; get load address
                jsr readHeader
                bne +
                iny
                jsr readHeader
                bne +               ; is load address zero?

                ; if load address is zero then overwrite with other load address
                ldy #$06            ; get header length
                jsr readHeader
                tax
                tay
                jsr readHeader      ; get load address low at offset $7c
                ldy #$08            ; set load address low
                sta (SID_HEADER_LO),y
                inx
                txa
                tay
                jsr readHeader      ; get load address high at offset $7d
                ldy #$09            ; set load address high
                sta (SID_HEADER_LO),y
+
                jsr getInitLo
                bne +
                jsr getInitHi
                bne +               ; is init address zero?

                ; init address is zero therefore overwrite it with load address
                ldy #$08            ; get load address low
                jsr readHeader
                ldy #$0a            ; set init address low
                sta (SID_HEADER_LO),y
                dey                 ; get load address high
                jsr readHeader
                ldy #$0b            ; set init address high
                sta (SID_HEADER_LO),y
+
                ; check if init is same as play, then ignore play address
                jsr getPlayLo
                sta $aa
                jsr getInitLo
                cmp $aa             ; compare with init low
                bne +
                jsr getPlayHi
                sta $aa
                jsr getInitHi
                cmp $aa             ; compare with init high
                bne +

                ; clear play address
                ldy #$0c
                lda #$00
                sta (SID_HEADER_LO),y
                iny
                sta (SID_HEADER_LO),y
+               rts

runPlayer       lda $aa
                pha
                lda $ab
                pha

                jsr cleanupMemory

                lda #<runRoutine
                ldx #>runRoutine
                ldy #runRoutineEnd - runRoutine
                jsr copyTo0100

                pla
                sta $ab
                pla
                sta $aa
                jmp $0100

copyChars       lda CHARROM_LOCATION
                sta $ff           ; charset destination address
                cmp #$10
                beq dontCopyChar

                lda #<CharROMCopy
                ldx #>CharROMCopy
                ldy #CharROMCopyEnd - CharROMCopy
                jsr copyTo0100

                lda #$d0
                sta $f8

                ldy #$00
                sty $fe
                sty $f7

copyChrLoop     jsr $0100
                iny
                bne copyChrLoop
                inc $ff
                inc $f8
                lda $f8
                cmp #$d4
                bne copyChrLoop
dontCopyChar    rts

getSecondSidAddress
                ldy #$7a
                jmp readHeader

getThirdSidAddress
                ldy #$7b
                jmp readHeader

printNumOfSongs lda SONG_TO_PLAY
                jsr extraPlayer.codeStart + extraPlayer.math.convertNumToDecDigit

                jsr writeSongNumber

                inc $fe
                lda #$2f
                ldy #$00
                jsr screenWrite
                inc $fe
                inc $fe

                ldy #$0e
                jsr readHeader
                sec
                sbc #$01

                jsr extraPlayer.codeStart + extraPlayer.math.convertNumToDecDigit

writeSongNumber stx $aa
                sty $ab

                ldy #$00
                jsr skipZeroDigit
                lda $aa
                jsr skipZeroDigit
                lda $ab
                jmp writeSongDigit

skipZeroDigit   cmp #'0'
                beq +
writeSongDigit  jsr screenWrite
                inc $fe
+               rts

; speed flag of tune 32 is also used for tunes 33 - 256
; (old implementation is not supported since current SID collections don't have them anymore)
; input: AC - song number to calculate the speed flag for
; speed flags most be set to $0100-$0103
calcSpeedFlag   pha
                ldy #$12            ; byte 4 of speed flags
                jsr readHeader
                sta $0100
                ldy #$13            ; byte 3 of speed flags
                jsr readHeader
                sta $0101
                ldy #$14            ; byte 2 of speed flags
                jsr readHeader
                sta $0102
                ldy #$15            ; byte 1 of speed flags
                jsr readHeader
                sta $0103
                pla

                cmp #32             ; check if less than 32
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
                lda $0100,y
-               dex
                bmi +
                lsr
                bpl -
+               and #$01
                rts

; input is high address for which the correct bank should be calculated
getBankInit     ldy #$7f  ;get load end address high
                jmp getBank
getBankPlay     ldy #$0d  ;get play address

getBank         and #$f0
                cmp #$d0
                bne isBankKernal

                lda #$34
                rts

isBankKernal    cmp #$e0
                beq bankKernal
                bcc isBankBasic

bankKernal      lda #$35
                rts

isBankBasic     jsr readHeader
                cmp #$a0
                beq bankBasic
                bcs bankBasic
                cpy #$7f            ; a load end page of $00 is the end at $10000 stored in 16 bits
                bne bankDefault
                cmp #$00
                beq bankBasic

bankDefault     lda #$37
                rts

bankBasic       jsr isRsid
                beq bankDefault
                lda #$36
                rts

getInitLo       ldy #$0a            ; get init address lo-byte (note that SID header is converted to little endian here)
                jmp readHeader

getInitHi       ldy #$0b            ; get init address hi-byte (note that SID header is converted to little endian here)
                jmp readHeader

getPlayLo       ldy #$0c            ; get play address lo-byte (note that SID header is converted to little endian here)
                jmp readHeader

getPlayHi       ldy #$0d            ; get play address hi-byte (note that SID header is converted to little endian here)
                jmp readHeader

;------ X is length
printData       lda SID_HEADER_LO
                sta $ac

                tya
                clc
                adc SID_HEADER_LO
                sta SID_HEADER_LO

                ldy #$00
fillData        jsr readHeader      ; the firmware has turned accented letters into plain ones
                beq stopPrintData

                sta $a6
                cmp #'_'            ; the C64 has no underscore: a line at the bottom of the cell
                bne conversionEnd
                lda #$64
                bne writeToScreen

conversionEnd   lda $a6
                and #$40
                bne +

                lda $a6
                and #$7f            ; only first 128 chars allowed, so map last 128 to first 128 chars
                bne writeToScreen

+               lda $a6
                and #$1f
writeToScreen   jsr screenWrite

                iny
                dex
                bne fillData
stopPrintData
                lda $ac
                sta SID_HEADER_LO
                rts

CharROMCopy     lda $01
                pha
                lda #$33
                sta $01
                lda ($f7),y
                pha
                lda #$34
                sta $01
                pla
                sta ($fe),y
                pla
                sta $01
                rts
CharROMCopyEnd

screenWriteBegin
                .logical $0100
screenWrite     pha
                lda $01
                sta screenBankTemp
                lda #$34
                sta $01
                pla
screenWriteAddress
                sta ($f7),y
                pha
                lda screenBankTemp
                sta $01
                pla
                rts
screenBankTemp
                .here
screenWriteEnd

readMem
                .logical $0180
readHeader      jsr saveBank
                lda (SID_HEADER_LO),y
                jmp restoreBank

readScreen      jsr saveBank
                lda ($b0),y
                jmp restoreBank

writeScreen     pha
                jsr saveBank
                pla
                sta ($b0),y
                jmp restoreBank

readAddress     jsr saveBank
                lda ($aa),y
                jmp restoreBank

writeAddress    pha
                jsr saveBank
                pla
                sta ($aa),y
                jmp restoreBank

saveBank        lda $01
                sta bankValue
                lda #$34
                sta $01
                rts

restoreBank     pha
                lda bankValue
                sta $01
                pla
                rts
bankValue
                .here
readMemEnd

runRoutine      lda #$40
                sta $dfff           ; turn off cartridge
                jmp ($00aa)
runRoutineEnd

turnOffCart     lda #$40
                sta $dfff           ; turn off cartridge
                rts

writeScreenData sta $fe
                sty $ff

-               ldy #$00
                lda ($fe),y
                beq endScreenWrite
                cmp #$ff
                bne writeData

                ldy #$02
                lda ($fe),y
                tax
                ldy #$01
                lda ($fe),y
                ldy #$00
-
                jsr screenWrite     ; write to screen
                inc $f7
                bne +
                inc $f8
+               dex
                bne -
                lda $fe
                clc
                adc #$03
                sta $fe
                bcc +
                inc $ff
+               bne --

writeData       jsr screenWrite     ; write to screen

                inc $f7
                bne +
                inc $f8
+               inc $fe
                bne +
                inc $ff
+               bne --
endScreenWrite  rts

setCurrentLineOffset
                lda CURRENT_LINE
                sta $fe
                lda #$28            ; multiply current line by 40
                sta $ff

                ldx #$08
                lda #$00
-               lsr
                ror $fe
                bcc +
                clc
                adc $ff
+               dex
                bpl -
                sta $ff
                rts

setCurrentLinePosition
                lda CURRENT_LINE
                jsr setCurrentLineOffset
                lda $ff
                clc
                adc SCREEN_LOCATION
                sta $ff
                rts

writeSidChipCount
                clc
                adc #$30
                dec $f8
                ldy #$100 - $23     ; column 5 of the line just written, $23 before $f7/$f8
                sta ($f7),y
                inc $f8
                rts

; printSingleSidInfo
;   input:
;   - AC: SID model
;   - XR: 0 = system info, 4 = system info without the clock,
;         1-3 = SID #1-#3 of the SID header
;   the clock is printed on the first line of each block only
printSingleSidInfo
                stx $ac             ; the kind of line, for the IRQ column below
                pha
                cpx #$02
                bcc checkVersion
                cpx #$04
                bcs checkVersion

                lda $fe
                clc
                adc #10
                sta $fe

                ; print SID address for second or third SID ($7a or $7b)
                txa
                clc
                adc #$78
                tay
                jsr readHeader
                jsr printHex

checkVersion    txa                 ; check if system info needs to be printed
                and #$03
                bne checkSidHeader1
                ; print system info
                pla                 ; detected SID model
                pha
                beq print8580
                cmp #$01
                beq print6581
                jmp printUnknownModel

checkSidHeader1 ldy #$04            ; check version
                jsr readHeader
                cmp #$01
                beq printUnknownModel

                pla
                pha
                and #$03
                beq printUnknownModel
                cmp #$01
                beq print6581
                cmp #$02
                beq print8580

                lda #<AnyLbl        ; print 'ANY', the tune plays on 6581 and 8580
                .byte $2c           ; skip the next instruction
print6581       lda #<S6581Lbl
                .byte $2c           ; skip the next instruction
print8580       lda #<S8580Lbl
                .byte $2c           ; skip the next instruction
printUnknownModel
                lda #<SUnknownLbl
                ldy #>SUnknownLbl   ; all labels share one page
printModel      sta $aa
                sty $ab
                pla
                txa
                pha
                jsr setCurrentLinePosition
                lda $fe
                clc
                adc #16
                sta $fe
                bcc +
                inc $ff
+
                jsr writeString

                ; print Clock info
                pla                 ; check if system info needs to be printed
                bne checkSidHeader2
                ; print system info
                lda C64_CLOCK
                and #$03
                beq printPal
                jmp printNtsc

checkSidHeader2 cmp #$02            ; SID #2 and #3 share the clock of SID #1
                bcs clockDone
                ldy #$04            ; check version
                jsr readHeader
                cmp #$01
                beq printUnknownClock

                ldy #$77
                jsr readHeader
                lsr
                lsr
                and #$03
                beq printUnknownClock
                cmp #$01
                beq printPal
                cmp #$02
                beq printNtsc

                lda #<AnyClockLbl   ; print '/ ANY', the tune plays on PAL and NTSC
                .byte $2c           ; skip the next instruction
printPal        lda #<PALLbl
                .byte $2c           ; skip the next instruction
printNtsc       lda #<NTSCLbl
                .byte $2c           ; skip the next instruction
printUnknownClock
                lda #<UnknownLbl
                ldy #>UnknownLbl    ; all labels share one page
printClock      sta $aa
                sty $ab
                lda $fe
                clc
                adc #8              ; ': ' in column 24, the video standard in column 26
                sta $fe
                bcc +
                inc $ff
+
                jsr writeString

                lda $ac             ; the IRQ only on the first NEEDS line
                beq clockDone
                lda $fe             ; ': ' in column 34, the IRQ in column 36
                clc
                adc #10
                ldx $ff
                bcc +
                inx
+               ldy #OFFSET_SPEED_SCREEN_LOCATION
                jsr setVariableWord

clockDone       inc CURRENT_LINE
                jmp setCurrentLinePosition

writeString     ldy #$00
-               lda ($aa),y
                beq +
                jsr $0100           ; write to screen
                iny
                bne -
+               rts

printHex        pha
                lsr
                lsr
                lsr
                lsr
                ldy #$00
                jsr printHexNibble
                pla
                and #$0f
printHexNibble
                cmp #$0a
                bcc +
                clc
                adc #+'A' - '0' - 10 - $40
+               adc #'0'
                sta ($fe),y
                iny
                rts

setupScreen     jsr copyChars

                lda CHARROM_LOCATION
                lsr
                lsr
                and #$0e
                ora #$01
                sta D018_VALUE

                lda SCREEN_LOCATION
                asl
                asl
                and #$f0
                ora D018_VALUE
                sta D018_VALUE

                lda SCREEN_LOCATION
                rol
                rol
                rol
                eor #$ff
                and #$03
                ora #$94
                sta DD00_VALUE

                lda #<screenWriteBegin
                ldx #>screenWriteBegin
                ldy #screenWriteEnd - screenWriteBegin
                jsr copyTo0100

                lda SCREEN_LOCATION
                sta $f8             ; $f7/$f8 is now screen address
                lda #$00
                sta $f7

                ; clear screen
                lda $f8
                pha
                ldy #$00
                ldx #$03
                lda #$20
-               jsr screenWrite
                iny
                cpx #$00
                bne +
                cpy #$e8
                beq ++
+               cpy #$00
                bne -
                inc $f8
                dex
                bpl -
+               pla
                sta $f8
                rts

; writeSystemLabel
;   input:
;   - YR: offset of an extra SID address in the SID header ($7a or $7b)
;   writes a numbered system label for that SID, if the SID header defines it
writeSystemLabel
                tya
                pha
                jsr readHeader
                beq +
                lda #<screenDataFound
                ldy #>screenDataFound
                jsr writeScreenData
                pla
                sec
                sbc #$78            ; $7a -> 2, $7b -> 3
                jmp writeSidChipCount
+               pla
                rts

; printSystemSidInfo
;   input:
;   - YR: offset of an extra SID address in the SID header ($7a or $7b)
;   prints address and detected model of that SID on its system line, if the SID header defines it
printSystemSidInfo
                jsr readHeader
                beq +
                pha

                lda $fe
                clc
                adc #10
                sta $fe

                pla
                pha
                jsr printHex        ; overwrite the $D400 of the label

                pla
                jsr detectSidModelAt
                ldx #$04            ; system info, the clock is on the first system line
                jmp printSingleSidInfo
+               rts

; detectSidModelAt
;   input:
;   - AC: address of the SID as the SID header stores it, $Dxx0 >> 4
;   output:
;   - AC: SID model, 00 = 8580, 01 = 6581, 02 = unknown or no SID there
;   The detection of the SID at $D400, done through a pointer so that it reaches
;   any address. The read through the pointer comes 7 cycles after the sawtooth
;   starts, not 4, so the frequency is $2800 rather than $4800: a 6581 has then
;   just made its first step, while an 8580, one cycle behind, has not. Where
;   no SID answers, or only a mirror of the one at $D400, it returns unknown.
detectSidModelAt
                pha
                asl
                asl
                asl
                asl
                sta $aa
                pla
                lsr
                lsr
                lsr
                lsr
                ora #$d0
                sta $ab

                lda #$ff            ; make sure the check is not done on a bad line
-               cmp $d012
                bne -
                lda #$28            ; sawtooth with the test bit, and the frequency
                ldy #$12
                sta ($aa),y
                ldy #$0f
                sta ($aa),y
                lda #$20            ; release the test bit: the sawtooth starts
                ldy #$12
                sta ($aa),y
                ldy #$1b
                lda ($aa),y         ; 7 cycles later: 1 on a 6581, 0 on an 8580
                tax
                and #$fe
                bne unknownSidAt
                lda ($aa),y         ; 18 cycles later: 2 on both
                cmp #$02
                bne unknownSidAt

                lda #$00            ; stop oscillator 3 at $D400: a mirror of it stands
                sta $d40e           ; still, a SID of its own counts on, at least one
                sta $d40f           ; step in the 7 cycles between the next two reads
                lda ($aa),y
                nop
                eor ($aa),y
                beq unknownSidAt
                txa
                beq +               ; an 8580 by its timing
                jsr checkCombined   ; a 6581 by its timing, but an UltiSID set to 8580 is one too
+               rts

unknownSidAt    lda #$02
                rts

; checkCombined
;   input:
;   - $aa/$ab: base address of a SID that its timing calls a 6581
;   output:
;   - AC: 00 = 8580, 01 = 6581
;   Sums 256 reads of OSC3 with triangle and sawtooth combined, at the frequency the
;   timing check left. On a 6581 the two nearly cancel, on an 8580 they do not, and an
;   UltiSID follows its "Combined Waveforms" setting. Measured on a C64 Ultimate, 3 x 256
;   reads each: real 6581 409-544, UltiSID as 6581 54-148, real 8580 5383-6094,
;   UltiSID as 8580 14835-15991. The combined waveforms are analog side effects and vary
;   from chip to chip, so this only ever turns a 6581 into an 8580, never back.
checkCombined
                ldy #$12
                lda #$30            ; triangle and sawtooth, gate off
                sta ($aa),y
                ldy #$1b
                lda #$00            ; AC = low byte of the sum, TEMP the high byte
                sta TEMP
                tax
-               adc ($aa),y         ; a carry from the low byte adds one more, too little to matter
                bcc +
                inc TEMP
+               dex
                bne -
                lda #$07            ; a high byte of 8 or more, 2048 or more, is an 8580
                cmp TEMP
                lda #$00
                rol
                rts

printSidInfo    lda $f7             ; restore sid header address
                sta SID_HEADER_LO

                lda $f8
                sta SID_HEADER_HI

                jsr setCurrentLinePosition

                ldy #$77
                jsr readHeader
                lsr
                lsr
                lsr
                lsr
                sta TEMP
                ldx #$01            ; first SID
                jsr printSingleSidInfo

                jsr getSecondSidAddress ; is second SID address defined?
                beq noMoreSids2

                ldy #$77
                jsr readHeader
                rol
                rol
                rol
                and #$03            ; bits 7-6 of the flags, the model of the second SID
                bne +
                lda TEMP            ; unknown SID model for second SID so use the info of first SID
+               ldx #$02            ; second SID
                jsr printSingleSidInfo

                ldy #$7b            ; is third SID address defined?
                jsr readHeader
                beq noMoreSids2

                ldy #$76
                jsr readHeader
                and #$03            ; bits 9-8 of the flags, the model of the third SID
                bne +
                lda TEMP            ; unknown SID model for third SID so use the info of first SID
+               ldx #$03            ; third SID
                jsr printSingleSidInfo
noMoreSids2
                inc CURRENT_LINE    ; the empty line between the two blocks
                jsr setCurrentLinePosition

                lda $fe             ; the first FOUND line, column 16: the advanced player rewrites it
                clc
                adc #16
                ldx $ff
                bcc +
                inx
+               ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr setVariableWord

                jsr detection.detectSystem
                sta C64_CLOCK
                stx SIDFX_DETECTED
                cpy #$01            ; a 6581 by its timing: check its combined waveforms
                bne +
                lda #$00
                sta $aa
                lda #$d4
                sta $ab
                jsr checkCombined
                tay
+               sty SID_MODEL

                tya
                ldx #$00            ; 0 indicates that system info is presented
                jsr printSingleSidInfo

                ldy #$7a            ; system info of the second SID
                jsr printSystemSidInfo
                ldy #$7b            ; system info of the third SID
                jsr printSystemSidInfo
                ; print number of songs
                inc CURRENT_LINE
                jsr setCurrentLinePosition
                lda $fe
                clc
                adc #8
                sta $fe
                bcc +
                inc $ff
+
                ldx $ff
                ldy #OFFSET_SONG_SCREEN_LOCATION
                jsr setVariableWord

                ; set sprite pointer
                jsr getScreenLocationLastHi
                asl
                asl
                ora #$02            ; since we want to have the sprite pointing at offset $0380 we can set bit 1  ($80 shr 6)

                ldy #$f8
                jsr writeScreen     ; screen address is already at SCREEN_LOCATION + $0300

                jsr printNumOfSongs
                pla
                rts

setExtraPlayerVars
                lda EXTRA_PLAYER_LOCATION
                sta relocator.BASE_ADDRESS

                jsr getScreenLocationLastHi
                ldx #extraPlayer.clockLoc - extraPlayer
                jsr setValueAt

                ldx #extraPlayer.songLenLoc1 - extraPlayer
                jsr setValueAt

                ldx #extraPlayer.songLenLoc2 - extraPlayer
                jsr setValueAt

                ldy #OFFSET_SONG_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldx #extraPlayer.songNumLoc - extraPlayer
                jsr setValueAt
                pla
                jsr writeNextAddress

                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldx #extraPlayer.sidModelLoc - extraPlayer
                jsr setValueAt
                pla
                jsr writeNextAddress

                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldx #extraPlayer.c64ModelLoc - extraPlayer
                jsr setValueAt
                pla
                jsr writeNextAddress

                ldy #OFFSET_SPEED_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldx #extraPlayer.speedLoc - extraPlayer
                jsr setValueAt
                pla
                jsr writeNextAddress

                ldy #OFFSET_SPEED_SCREEN_LOCATION
                jsr getVariableWord ; XR/AC: the first NEEDS line, column 34
                sec
                sbc SCREEN_LOCATION
                ora #$d8            ; the same place in colour RAM
                sta TEMP
                txa
                sec
                sbc #40 + 34        ; the headings are the row above it, from column 0
                bcs +
                dec TEMP
+               ldx #extraPlayer.headingsLoc - extraPlayer
                jsr setValueAt
                lda TEMP
                jsr writeNextAddress

                lda player.offSongNr
                ldx #extraPlayer.songNumSet - extraPlayer
                jsr setValueAt
                lda player.offSongNr + 1
                jsr writeAtPlayerLocation

                lda #$80
                ldx #extraPlayer.spriteLoc - extraPlayer
                jsr setValueAt
                jsr getScreenLocationLastHi
                jsr writeNextAddress

                lda player.offPlayLoop
                ldx #extraPlayer.playerLoopLoc - extraPlayer
                jsr setValueAt
                lda player.offPlayLoop + 1
                jsr writeAtPlayerLocation

                lda player.offExtraPlay1
                ldx #extraPlayer.epCallLoc - extraPlayer
                jsr setValueAt
                lda player.offExtraPlay1 + 1
                jsr writeAtPlayerLocation

                lda player.offFastForw
                ldx #extraPlayer.fastFwd - extraPlayer
                jsr setValueAt
                lda player.offFastForw + 1
                jsr writeAtPlayerLocation

                lda player.offPause
                ldx #extraPlayer.pauseKey - extraPlayer
                jsr setValueAt
                lda player.offPause + 1
                jsr writeAtPlayerLocation

                lda CANT_PAUSE
                bne +
                jsr isRsid
+               ldy extraPlayer.cantPauseLoc        ; can't pause tune when it's an RSID tune or when play address is zero
                ldx extraPlayer.cantPauseLoc + 1
                jsr setValue

                lda player.offSpeed1
                ldx #extraPlayer.hdrSpeedFlag1 - extraPlayer
                jsr setValueAt
                lda player.offSpeed1 + 1
                jsr writeAtPlayerLocation

                lda player.offSpeed2
                ldx #extraPlayer.hdrSpeedFlag2 - extraPlayer
                jsr setValueAt
                lda player.offSpeed2 + 1
                jsr writeAtPlayerLocation

                lda player.offSpeed3
                ldx #extraPlayer.hdrSpeedFlag3 - extraPlayer
                jsr setValueAt
                lda player.offSpeed3 + 1
                jsr writeAtPlayerLocation

                jsr getSecondSidAddress
                beq +
                ldy extraPlayer.sid2Vol
                ldx extraPlayer.sid2Vol + 1
                jsr writeSidVolAddr

+               jsr getThirdSidAddress
                beq +
                ldy extraPlayer.sid3Vol
                ldx extraPlayer.sid3Vol + 1
                jsr writeSidVolAddr

+               ldy #$12            ; byte 4 of speed flags
                jsr readHeader
                ldx #extraPlayer.hdrSpeedFlags - extraPlayer
                jsr setValueAt
                ldy #$13            ; byte 3 of speed flags
                jsr readHeader
                ldy #$01
                jsr writeAddress
                ldy #$14            ; byte 2 of speed flags
                jsr readHeader
                ldy #$02
                jsr writeAddress
                ldy #$15            ; byte 1 of speed flags
                jsr readHeader
                ldy #$03
                jsr writeAddress

                lda #<player.playerMain
                ldx #extraPlayer.playerLoc - extraPlayer
                jsr setValueAt
                lda PLAYER_LOCATION
                jsr writeNextAddress

                ldy #$77
                jsr readHeader
                lsr
                lsr
                and #$03
                tax
                and #$01
                bne +               ; when PAL flag is set then always write 0 (therefore jump to lsr)
                txa
+               lsr                 ; value is 1 for NTSC, otherwise 0 for PAL / UNKNOWN clock. If PAL is set then value is always 0.
                ldx #extraPlayer.c64ModelFlag - extraPlayer
                jsr setValueAt

                jsr isRsid          ; an RSID tune sets up its own interrupt
                ldx #extraPlayer.rsidFound - extraPlayer
                jsr setValueAt

                lda SID_MODEL
                ldx #extraPlayer.sidModelFound - extraPlayer
                jsr setValueAt

                lda SONG_TO_PLAY
                ldx #extraPlayer.songNum - extraPlayer
                jmp setValueAt

setupSldb       ldy #$0e            ; get number of songs
                jsr readHeader
                sec
                sbc #$01
                ldx #extraPlayer.maxSongLoc - extraPlayer
                jsr setValueAt

                tax                 ; XR = number of songs

                lda extraPlayer.songLenData
                sta $aa
                lda extraPlayer.songLenData + 1
                clc
                adc relocator.BASE_ADDRESS
                sta $ab

                jmp songlengths.loadSongLengths

relocateExtraPlayer
                lda EXTRA_PLAYER_LOCATION
                sta relocator.START_HI
                lda #$00
                sta relocator.START_LO
                lda extraPlayer.codeSize
                sta relocator.END_LO
                lda extraPlayer.codeSize + 1
                clc
                adc relocator.START_HI
                sta relocator.END_HI
                jmp relocator.relocateCode

setPlayerVars   lda PLAYER_LOCATION
                sta relocator.BASE_ADDRESS

                lda SONG_TO_PLAY
                ldy player.offSongNr
                ldx player.offSongNr + 1
                jsr setValue
                ldy player.offBasSongNr
                ldx player.offBasSongNr + 1
                jsr setValue

                ; handle init address
                jsr getInitHi
                pha
                jsr getInitLo
                ldy player.offInit
                ldx player.offInit + 1
                jsr setValue
                pla
                jsr writeNextAddress

                ; handle play address
                jsr getPlayHi
                pha
                jsr getPlayLo
                ldy player.offPlay
                ldx player.offPlay + 1
                jsr setValue
                pla
                jsr writeNextAddress

                ldy player.offReloc1
                lda player.offReloc1 + 1
                jsr relocator.relocByte

                ldy player.offCiaLU1
                lda player.offCiaLU1 + 1
                jsr relocator.relocByte

                ldy player.offCiaLU2
                lda player.offCiaLU2 + 1
                jsr relocator.relocByte

                ldy player.offCiaFix
                lda player.offCiaFix + 1
                jsr relocator.relocByte

                ldy player.offCiaFix1
                lda player.offCiaFix1 + 1
                jsr relocator.relocByte

                ldy player.offCiaFix2
                lda player.offCiaFix2 + 1
                jsr relocator.relocByte

                ldy player.offHiIrq
                lda player.offHiIrq + 1
                jsr relocator.relocByte

                ldy player.offHiBrk
                lda player.offHiBrk + 1
                jsr relocator.relocByte

                ldy player.offHiAdvInit
                lda player.offHiAdvInit + 1
                jsr relocator.relocByte

                lda relocator.BASE_ADDRESS
                ldy player.offBasicEnd
                ldx player.offBasicEnd + 1
                jsr setValue

                lda relocator.BASE_ADDRESS
                ldy player.offHiPlayer
                ldx player.offHiPlayer + 1
                jsr setValue

                jsr isRsid
                ldy player.offRsid
                ldx player.offRsid + 1
                jsr setValue

                cmp #$01
                bne +
                jsr setSpeedFlags             ; set all speed flags to 1
+
                jsr isBasic
                ldy player.offBasic
                ldx player.offBasic + 1
                jsr setValue
                cmp #$00
                beq noBasic

                ; it's a BASIC tune so jump to $A7AE
                lda #$ae
                ldy player.offInit
                ldx player.offInit + 1
                jsr setValue
                lda #$a7
                jsr writeNextAddress

noBasic         ldy #$7e            ; read lo-byte of load end address
                jsr readHeader
                ldy player.offLoEnd
                ldx player.offLoEnd + 1
                jsr setValue

                ldy #$7f            ; read hi-byte of load end address
                jsr readHeader
                ldy player.offhiEnd
                ldx player.offhiEnd + 1
                jsr setValue

                jsr getInitHi
                jsr getBankInit
                ldy player.offInitBank
                ldx player.offInitBank + 1
                jsr setValue

                jsr getPlayHi
                jsr getBankPlay
                ldy player.offPlayBank
                ldx player.offPlayBank + 1
                jsr setValue

                jsr isPlayZero
                bne playNotZero

                ; play address is zero
                lda #$01
                ldy player.offPlayNull1
                ldx player.offPlayNull1 + 1
                jsr setValue
                ldy player.offPlayNull2
                ldx player.offPlayNull2 + 1
                jsr setValue

                lda #$ea            ; NOP
                ldy player.offInitAfter
                ldx player.offInitAfter + 1
                jsr setValue
                jsr writeNextAddress ; write another NOP
                jmp continueInitPlayer

playNotZero     lda #$85            ; STA $01
                ldy player.offInitAfter
                ldx player.offInitAfter + 1
                jsr setValue
                lda #$01
                jsr writeNextAddress

continueInitPlayer
                lda D018_VALUE
                ldy player.offD018
                ldx player.offD018 + 1
                jsr setValue

                lda DD00_VALUE
                ldy player.offDD00
                ldx player.offDD00 + 1
                jsr setValue

                ldy #$77
                jsr readHeader
                lsr
                lsr
                and #$03
                tax
                and #$01
                bne +                 ; when PAL flag is set then always write 0 (therefore jump to lsr)
                txa
+               lsr                   ; value is 1 for NTSC, otherwise 0 for PAL / UNKNOWN clock. If PAL is set then value is always 0.
                ldy player.offClock
                ldx player.offClock + 1
                jsr setValue

                lda C64_CLOCK
                ldy player.offPalNtsc
                ldx player.offPalNtsc + 1
                jsr setValue

                lda #$00
                sta CANT_PAUSE

                ; speed flags
                lda SONG_TO_PLAY    ; read current song
                jsr calcSpeedFlag
                jsr setSpeedFlags

                cmp #$01            ; do not use extra player when speed flag is 1
                bne +
                jmp noExtraPlayWithFF

+               lda EXTRA_PLAYER_LOCATION
                bne +
                jmp noExtraPlayer
+               jmp enableExtraPlayerCalls

                .enc 'screen'
                .if (* & $ff) >= $de - 38 ; the 39 bytes of labels below must share one page and stay below $DE, see the checks
                .align $100
                .fi
PALLbl          .text ': PAL', 0
NTSCLbl         .text ': NTSC', 0
AnyClockLbl     .text ': '
AnyLbl          .text 'ANY', 0
S6581Lbl        .text '6581', 0
S8580Lbl        .text '8580', 0
UnknownLbl      .text ': '
SUnknownLbl     .text 'UNKNOWN', 0
                .cerror (PALLbl >> 8) != (SUnknownLbl >> 8), 'the model and clock labels must share one page'
                ; a .byte $2c above turns lda #<label into BIT $xxA9, xx the label's low byte: for $D0-$DD
                ; that reads a VIC, SID, colour RAM or CIA register (9, TOD seconds) without side effects,
                ; but $DE and $DF would read the cartridge's I/O
                .cerror (PALLbl & $ff) <= $df && (SUnknownLbl & $ff) >= $de, 'a skipped lda #<label must not make the BIT read cartridge I/O'

                .enc 'none'

SIDMagic        .text 'SID'

