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
                cmp #$f0
                bne isBankBasic
bankKernal      lda #$35
                rts
isBankBasic     jsr readHeader
                and #$f0
                bpl bankDefault
                cmp #$80
                beq bankDefault
                cmp #$90
                beq bankDefault
                lda #$36
                rts
bankDefault     lda #$37
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

                lda #<ASCII
                sta $a9
                lda #>ASCII
                sta $aa
                lda #<PETSCII
                sta $a7
                lda #>PETSCII
                sta $a8

                ldy #$00
fillData        jsr readHeader
                beq stopPrintData

                ; ascii special character to petscii conversion
                sta $a6
                sty $a5             ; save index

                ldy #$00
charConvLoop    lda ($a9),y
                beq conversionEnd
                cmp $a6
                bne checkNextChar

                lda ($a7),y
                sta $a6
                bne writeToScreen

checkNextChar   iny
                bne charConvLoop

conversionEnd   lda $a6
                and #$40
                bne +

                lda $a6
                and #$7f            ; only first 128 chars allowed, so map last 128 to first 128 chars
                bne writeToScreen

+               lda $a6
                and #$1f
writeToScreen   ldy $a5
                jsr screenWrite

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
                pha
                lda $f7
                sec
                sbc #$24
                sta $f7
                bcs +
                dec $f8
+
                lda #'#'
                ldy #$00
                sta ($f7),y
                iny

                pla
                clc
                adc #$30
                sta ($f7),y

                lda $f7
                clc
                adc #$24
                sta $f7
                bcc +
                inc $f8
+               rts

printSingleSidInfo
                pha
                txa
                ; check for which SID number to print the info
                beq checkVersion
                cmp #$01
                beq checkVersion

                cmp #$02
                bne +

                lda $fe
                clc
                adc #10
                sta $fe

                ; print SID address for second SID
                jsr getSecondSidAddress
                jsr printHex
                jmp checkVersion

+               cmp #$03
                bne checkVersion

                lda $fe
                clc
                adc #10
                sta $fe

                ; print SID address for third SID
                jsr getThirdSidAddress
                jsr printHex

checkVersion    txa                 ; check if system info needs to be printed
                bne checkSidHeader1
                ; print system info
                lda SID_MODEL
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

                lda #<S65818580Lbl  ; print '6581 / 8580'
                ldy #>S65818580Lbl
                jmp printModel

print6581       lda #<S6581Lbl
                ldy #>S6581Lbl
                jmp printModel

print8580       lda #<S8580Lbl
                ldy #>S8580Lbl
                jmp printModel

printUnknownModel
                lda #<SUnknownLbl
                ldy #>SUnknownLbl
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

                iny
                sty $ac

                ; print Clock info
                pla                 ; check if system info needs to be printed
                bne checkSidHeader2
                ; print system info
                lda C64_CLOCK
                and #$03
                beq printPal
                jmp printNtsc

checkSidHeader2 ldy #$04            ; check version
                jsr readHeader
                cmp #$01
                beq printUnknownClock

                ldy #$77
                jsr readHeader
                lsr
                lsr
                and #$03
                cmp #$00
                beq printUnknownClock
                cmp #$01
                beq printPal
                cmp #$02
                beq printNtsc

                lda #<PALNTSCLbl    ; print 'PAL / NTSC'
                ldy #>PALNTSCLbl
                jmp printClock

printPal        lda #<PALLbl
                ldy #>PALLbl
                jmp printClock

printNtsc       lda #<NTSCLbl
                ldy #>NTSCLbl
                jmp printClock

printUnknownClock
                lda #<UnknownLbl
                ldy #>UnknownLbl
printClock      sta $aa
                sty $ab
                lda $fe
                clc
                adc $ac
                sta $fe
                bcc +
                inc $ff
+
                jsr writeString

                inc CURRENT_LINE
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

printSidInfo    lda $f7             ; restore sid header address
                sta SID_HEADER_LO

                lda $f8
                sta SID_HEADER_HI

                jsr setCurrentLinePosition

                jsr detection.detectSystem
                sta C64_CLOCK
                sty SID_MODEL
                stx SIDFX_DETECTED

                ldx #$00            ; 0 indicates that system info is presented
                jsr printSingleSidInfo

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
                cmp #$00
                bne +
                lda TEMP            ; unknown SID model for second SID so use the info of first SID
+               ldx #$02            ; second SID
                jsr printSingleSidInfo

                ldy #$7b            ; is third SID address defined?
                jsr readHeader
                beq noMoreSids2

                ldy #$76
                jsr readHeader
                cmp #$00
                bne +
                lda TEMP            ; unknown SID model for third SID so use the info of first SID
+               ldx #$03            ; third SID
                jsr printSingleSidInfo
noMoreSids2
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
                ldy extraPlayer.clockLoc
                ldx extraPlayer.clockLoc + 1
                jsr setValue

                ldy extraPlayer.songLenLoc1
                ldx extraPlayer.songLenLoc1 + 1
                jsr setValue

                ldy extraPlayer.songLenLoc2
                ldx extraPlayer.songLenLoc2 + 1
                jsr setValue

                ldy #OFFSET_SONG_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldy extraPlayer.songNumLoc
                ldx extraPlayer.songNumLoc + 1
                jsr setValue
                pla
                jsr writeNextAddress

                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldy extraPlayer.sidModelLoc
                ldx extraPlayer.sidModelLoc + 1
                jsr setValue
                pla
                jsr writeNextAddress

                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldy extraPlayer.c64ModelLoc
                ldx extraPlayer.c64ModelLoc + 1
                jsr setValue
                pla
                jsr writeNextAddress

                lda player.offSongNr
                ldy extraPlayer.songNumSet
                ldx extraPlayer.songNumSet + 1
                jsr setValue
                lda player.offSongNr + 1
                jsr writeAtPlayerLocation

                lda #$80
                ldy extraPlayer.spriteLoc
                ldx extraPlayer.spriteLoc + 1
                jsr setValue
                jsr getScreenLocationLastHi
                jsr writeNextAddress

                lda player.offPlayLoop
                ldy extraPlayer.playerLoopLoc
                ldx extraPlayer.playerLoopLoc + 1
                jsr setValue
                lda player.offPlayLoop + 1
                jsr writeAtPlayerLocation

                lda player.offExtraPlay1
                ldy extraPlayer.epCallLoc
                ldx extraPlayer.epCallLoc + 1
                jsr setValue
                lda player.offExtraPlay1 + 1
                jsr writeAtPlayerLocation

                lda player.offFastForw
                ldy extraPlayer.fastFwd
                ldx extraPlayer.fastFwd + 1
                jsr setValue
                lda player.offFastForw + 1
                jsr writeAtPlayerLocation

                lda player.offPause
                ldy extraPlayer.pauseKey
                ldx extraPlayer.pauseKey + 1
                jsr setValue
                lda player.offPause + 1
                jsr writeAtPlayerLocation

                lda CANT_PAUSE
                bne +
                jsr isRsid
+               ldy extraPlayer.cantPauseLoc        ; can't pause tune when it's an RSID tune or when play address is zero
                ldx extraPlayer.cantPauseLoc + 1
                jsr setValue

                lda player.offSpeed1
                ldy extraPlayer.hdrSpeedFlag1
                ldx extraPlayer.hdrSpeedFlag1 + 1
                jsr setValue
                lda player.offSpeed1 + 1
                jsr writeAtPlayerLocation

                lda player.offSpeed2
                ldy extraPlayer.hdrSpeedFlag2
                ldx extraPlayer.hdrSpeedFlag2 + 1
                jsr setValue
                lda player.offSpeed2 + 1
                jsr writeAtPlayerLocation

                lda player.offSpeed3
                ldy extraPlayer.hdrSpeedFlag3
                ldx extraPlayer.hdrSpeedFlag3 + 1
                jsr setValue
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
                ldy extraPlayer.hdrSpeedFlags
                ldx extraPlayer.hdrSpeedFlags + 1
                jsr setValue
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
                ldy extraPlayer.playerLoc
                ldx extraPlayer.playerLoc + 1
                jsr setValue
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
                ldy extraPlayer.c64ModelFlag
                ldx extraPlayer.c64ModelFlag + 1
                jsr setValue

                lda SIDFX_DETECTED
                ldy extraPlayer.sidFxFound
                ldx extraPlayer.sidFxFound + 1
                jsr setValue

                lda SID_MODEL
                ldy extraPlayer.sidModelFound
                ldx extraPlayer.sidModelFound + 1
                jsr setValue

                lda SONG_TO_PLAY
                ldy extraPlayer.songNum
                ldx extraPlayer.songNum + 1
                jmp setValue

setupSldb       ldy #$0e            ; get number of songs
                jsr readHeader
                sec
                sbc #$01
                ldy extraPlayer.maxSongLoc
                ldx extraPlayer.maxSongLoc + 1
                jsr setValue

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
PALLbl          .text '/ PAL', 0
NTSCLbl         .text '/ NTSC', 0
PALNTSCLbl      .text '/ PAL / NTSC', 0
UnknownLbl      .text '/ UNKOWN', 0
S6581Lbl        .text '6581', 0
S8580Lbl        .text '8580', 0
S65818580Lbl    .text '6581 / 8580', 0
SUnknownLbl     .text 'UNKOWN', 0

                ;     'ÀÁÂÃÄÅÆàáâãäåæÈÉÊËèéêëÌÍÎÏìíîïÒÓÔÕÖØòóôõöøÙÚÛÜùúûüÇçÑñÝŸýÿß'
PETSCII         .text 'AAAAAAAAAAAAAAEEEEEEEEIIIIIIIIOOOOOOOOOOOOUUUUUUUUCCNNYYYYB'
                .byte $64   ; PETSCII underscore
                .byte 0
                .enc 'none'

SIDMagic        .text 'SID'

ASCII           .byte $c0, $c1, $c2, $c3, $c4, $c5, $c6             ; all A variants
                .byte $e0, $e1, $e2, $e3, $e4, $e5, $e6             ; all a variants
                .byte $c8, $c9, $ca, $cb                            ; all E variants
                .byte $e8, $e9, $ea, $eb                            ; all e variants
                .byte $cc, $cd, $ce, $cf                            ; all I variants
                .byte $ec, $ed, $ee, $ef                            ; all i variants
                .byte $d2, $d3, $d4, $d5, $d6, $d8                  ; all O variants
                .byte $f2, $f3, $f4, $f5, $f6, $f8                  ; all o variants
                .byte $d9, $da, $db, $dc                            ; all U variants
                .byte $f9, $fa, $fb, $fc                            ; all u variants
                .byte $c7, $e7, $d1, $f1, $dd, $9f, $fd, $ff, $df   ; other chars
                .byte '_'
                .byte 0

                ;examples special characters:
                ; /HVSC/C64Music/MUSICIANS/W/Walt/Maelkeboetten.sid (Mæclkebøtten)
                ; /HVSC/C64Music/MUSICIANS/A/Ass_It/Lasst_Uns_Froh.sid (Laßt Uns Froh)
                ; /HVSC/C64Music/GAMES/A-F/Captain_Blood.sid (by François Lionet)
                ; /HVSC/C64Music/MUSICIANS/D/Da_Blondie/Brain_Damage.sid (by Attila Szõke)
                ; etc.
