;-----------------------------------------------------------------------
; Ultimate SID Player V2.0b - Sidplayer for the Ultimate hardware
;
; Written by Wilfred Bos - April 2009
;                          August 2017 - January 2018
;
; Copyright (c) 2009 - 2019 Wilfred Bos / Gideon Zweijtzer
;
; Info:
;  Editing the source code should be done in a text editor which supports UTF-8.
;
;  Call the player by activating the cartridge and performing a reset. Pass tune
;  selection in the SID header at offset $10 (default song)
;  The player needs a SID header which is located somewhere in memory. The SID
;  header should be located at address $XX00. The location of the header should
;  be passed at address $0164/$0165 after a DMA load of the song + header data.
;  The version ($04), data offset ($06), load address ($08), init address ($0A)
;  play address ($0C), songs ($0e) and startSong ($10) in the SID header should
;  be set in little endian format. The original SID header is in big endian
;  format.
;  The SID header should include the load address at offset $7c (if present).
;  Therefore just copy the original SID header till offset $7e in front of the
;  player. Set the load end address of the SID tune after the SID header at $7e
;  in little endian format.
;
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

                * = $8000           ; base address of cartridge

                .byte <start        ; cold start vector
                .byte >start
                .byte <startNMI     ; nmi vector
                .byte >startNMI
                .byte 'C' + $80, 'B' + $80, 'M' + $80, '8', '0'   ; CBM80

                .text 0, 'Ultimate SID Player Cartridge V2.0b - Copyright (c) 2009-2019 Wilfred Bos / Gideon Zweijtzer', 0

startNMI        pla                 ; ignore pressing restore key
                tay
                pla
                tax
                pla
                rti

start           sei
                lda #$7f
                sta $dd0d
                lda $dd0d

                lda #$00
                sta $d020
                sta $d021
                sta $d418

                jsr screenOff

                jsr $ff84           ; init I/O devices / CIA timers
                jsr alternative_ff87
                jsr $ff8a           ; restore default IO vectors

                jsr resetSID

                lda $02
                cmp #SID_MODE
                beq sidMode
                cmp #DMA_MODE
                beq dmaMode
                jmp reset

dmaMode         lda #$00            ; load PRG file
                sta $02

                jsr $ff81           ; (init screen and VIC-Chip the normal way)
                jmp +

sidMode         jsr alternative_ff81

+               jsr $e453           ; copy BASIC vectors to RAM

                lda $0300
                sta $0334
                lda $0301
                sta $0335
                lda #<basicStarted
                sta $0300
                lda #>basicStarted
                sta $0301
                lda $a001
                sta $0101
                lda $a000
                clc
                adc #$03            ; skip JSR $E453 to avoid overwriting vector at $0300
                sta $0100
                bcc +
                inc $0101
                cli
+               jmp ($0100)

alternative_ff87
                ; jsr $FF87 / $FD50 (init RAM, tape buffer, screen) is replaced by the following block
                lda #$00
                tay
-               sta $0200,y
                iny
                bne -
                ldx #$3c
                ldy #$03
                stx $b2
                sty $b3

                lda #$A0
                sta $0284
                lda #$08
                sta $0282
                lsr
                sta $0288
                rts

alternative_ff81
                ; jsr $FF81 (init screen and VIC-Chip) is replaced by the following block to avoid screen color change
                lda #$03
                sta $9a
                lda #$00
                sta $99
                ldx #$2f
-               lda $ecb8,x
                sta $cfff,x
                cpx #$23            ; don't set $d020 and $d021
                bne +
                dex
                dex
+               cpx #$13            ; don't set d011
                bne +
                dex
+               dex
                bne -

                jsr $e51b
                jmp $ff5e

basicStarted    sei
                lda #<basicStartedIRQ
                sta $0314
                lda #>basicStartedIRQ
                sta $0315

                lda $0334
                sta $0300
                lda $0335
                sta $0301
                jmp ($0300)

basicStartedIRQ
                lda #$31            ; restore IRQ vector
                sta $0314
                lda #$ea
                sta $0315

                lda $dc0d           ; acknowledge IRQ

                lda #<initMem
                ldx #>initMem
                ldy #initMemEnd - initMem
                jsr copyTo0100

                lda $02
                cmp #SID_MODE
                beq loadSid
                jmp $0100           ; clear memory and execute DMA load, and do run

reset           lda #<resetRoutine
                ldx #>resetRoutine
                ldy #resetRoutineEnd - resetRoutine
                jmp runAt0100

loadSid         ldx #$ff            ; init stack pointer
                txs
                lda #$fc            ; push reset address on stack
                pha
                lda #$e2 - 1
                pha

                jsr $0100           ; clear memory and execute DMA load

                ; important: retrieve value at $0165 after DMA load
                lda $0165           ; get high address of SID header
                sta SID_HEADER_HI   ; store SID header address hi-byte
                lda #$00
                sta SID_HEADER_LO

                ldx #$00
-               lda readMem,x
                sta $0180,x
                inx
                cpx #readMemEnd - readMem
                bne -

                jsr moveSidHeader   ; move to location where it can be modified

                ldy #$03
-               jsr readHeader
                cmp SIDMagic - 1,y ; detect if header is a real SID header
                bne reset           ; header is no real header so perform a reset
                dey
                bne -

;##################################################

                jsr fixHeader       ; fix header after it has been moved (temporary fix)
                jsr prepareSidHeader

                ldy #$10            ; default song offset
                jsr readHeader
                sta SONG_TO_PLAY    ; set song to play

                jsr calculateExtraPlayerSize
                jsr calculateLocations

                jsr setupScreen
                jsr songlengths.displayCurSongLength

                jsr copyPlayer
                jsr setPlayerVars

                lda SCREEN_LOCATION
                and #$f0
                cmp #$d0
                bne +
                ; don't allow screen updates when the screen is located at $Dxxx and therefore the extra player should not be activated
                lda #$00
                sta EXTRA_PLAYER_LOCATION
+
                lda EXTRA_PLAYER_LOCATION
                beq +
                jsr copyExtraPlayer
                jsr relocateExtraPlayer
                jsr setExtraPlayerVars
+
                lda PLAYER_LOCATION
                sta $ab
                lda #$00
                sta $aa
                jmp runPlayer

;##################################################

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

                lda $ab
                sta EXTRA_PLAYER_SIZE
                rts

fixHeader       ; load end address is always 2 bytes off (needs to be fixed in the Ultimate firmware)
                ldy #$7e
                jsr readHeader
                clc
                adc #$02
                sta (SID_HEADER_LO),y
                bcc +
                iny
                jsr readHeader
                clc
                adc #$01
                sta (SID_HEADER_LO),y
+
                ldy #$04            ; read version of header
                jsr readHeader
                cmp #$03
                bcs +

                ; remove info about 2nd and 3rd SID when header version if not 3 or higher
                lda #$00
                ldy #$7a            ; remove 2nd SID address
                sta (SID_HEADER_LO),y
                iny                 ; remove 2nd SID address
                sta (SID_HEADER_LO),y
+               rts

moveSidHeader   ldy #$7d            ; read hi byte of load address
                jsr readHeader
                cmp #$04            ; if load address is lower than $0400 then move header to $FF00
                bcc moveToEndOfMem

                lda SID_HEADER_HI
                cmp #$03
                beq noMoveNeeded

                ldy #$7f
-               lda (SID_HEADER_LO),y
                sta $0380,y
                lda #$00            ; clean previous location
                sta (SID_HEADER_LO),y
                dey
                bpl -
                lda #$80
                ldy #$03
                jmp setSidHeaderAddr

moveToEndOfMem  lda SID_HEADER_HI
                cmp #$ff
                beq noMoveNeeded

                ldy #$7f
-               lda (SID_HEADER_LO),y
                sta $ff00,y
                lda #$00            ; clean previous location
                sta (SID_HEADER_LO),y
                dey
                bpl -
                lda #$00
                ldy #$ff
setSidHeaderAddr
                sta SID_HEADER_LO
                sty SID_HEADER_HI
noMoveNeeded    rts

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

calculateLocations
                ldy #$78            ; get free page base
                jsr readHeader
                beq calcLocBasedOnLoadArea
                cmp #$ff
                beq noAreaPossible
                ldy #$79            ; get free page size
                jsr readHeader
                beq calcLocBasedOnLoadArea
                cmp #$01
                bne calcLocBasedOnFreePages

calcLocBasedOnLoadArea
                jsr memalloc.calcPlayerLocations
                jmp memalloc.calcExtraPlayerLocation

noAreaPossible  lda #$00
                sta EXTRA_PLAYER_LOCATION
                jmp memalloc.calcSmallestPlayerLocation

calcLocBasedOnFreePages
                jsr memalloc.calcPlayerScreenLocForRelocArea
                jmp memalloc.calcExtraPlayerLocForRelocArea

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
                jsr setValue

                ldy #$0e            ; get number of songs
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
                bne enableExtraPlayerCalls
                jmp noExtraPlayer

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
                pha
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

                lda #<screenData1
                ldy #>screenData1
                jsr writeScreenData

                jsr canReleaseFieldSplit
                cpy #$00
                beq +

                ; write year label
                lda #<screenData2
                ldy #>screenData2
                jsr writeScreenData
+
                ; write empty line
                lda #<screenData3
                ldy #>screenData3
                jsr writeScreenData

                ; write system label
                lda #<screenData4
                ldy #>screenData4
                jsr writeScreenData

                ; write SID label
                lda #<screenData5
                ldy #>screenData5
                jsr writeScreenData

                jsr getSecondSidAddress ; is second SID address defined?
                beq noMoreSids

                lda #1
                jsr writeSidChipCount

                ; write SID label
                lda #<screenData5
                ldy #>screenData5
                jsr writeScreenData

                lda #2
                jsr writeSidChipCount

                jsr getThirdSidAddress ; is third SID address defined?
                beq noMoreSids

                ; write SID label
                lda #<screenData5
                ldy #>screenData5
                jsr writeScreenData

                lda #3
                jsr writeSidChipCount
noMoreSids
                ; write SONG label
                lda #<screenData6
                ldy #>screenData6
                jsr writeScreenData

                lda #(40 * 24) >> 8
                clc
                adc SCREEN_LOCATION
                sta $f8
                lda #(40 * 24) & $ff
                sta $f7

                ; write time bar
                lda #<screenData7
                ldy #>screenData7
                jsr writeScreenData

                pla
                pha
                sta $ff             ; $fe/$ff is now screen address
                lda #$00
                sta $fe

                lda #$fe            ; $fe/$ff are now used to write to screen
                sta screenWriteAddress + 1

                lda SID_HEADER_LO   ; backup sid header address
                sta $f7

                lda SID_HEADER_HI
                sta $f8

                ldy #$16            ; offset title in SID header

                lda #8 + 40 * 3
                sta $fe
                ldx #32             ; print max 32 chars
                jsr printData       ; write title data to screen

                ldy #$36            ; offset author in SID header

                lda #8 + 40 * 4
                sta $fe
                ldx #32             ; print max 32 chars
                jsr printData       ; write author data to screen

                jsr canReleaseFieldSplit
                cpy #$00
                bne splitReleasedField

                ldy #$56            ; offset released in SID header
                lda #8 + 40 * 5
                sta $fe
                ldx #32             ; print max 32 chars
                jsr printData       ; write released data to screen

                lda #(16 + 40 * 7) >> 8
                clc
                adc SCREEN_LOCATION
                tax
                lda #(16 + 40 * 7) & $ff
                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr setVariableWord

                lda #$07
                sta CURRENT_LINE
                jmp printSidInfo

canReleaseFieldSplit
                ldy #$56            ; offset released in SID header
                jsr readHeader
                cmp #$31            ; starts released field with 1?
                beq +
                cmp #$32            ; starts released field with 2?
                bne cannotSplit
+
                ldy #$5a            ; search for space
                jsr readHeader
                cmp #$20
                beq +
                ldy #$5d
                jsr readHeader
                cmp #$20
                beq +
cannotSplit     ldy #00
+               rts

getSecondSidAddress
                ldy #$7a
                jmp readHeader

getThirdSidAddress
                ldy #$7b
                jmp readHeader

splitReleasedField
                tya
                sec
                sbc #$56
                pha

                iny                 ; start index in header of publisher

                eor #$ff            ; calculate size of publisher by 32 - start index
                clc
                adc #32
                tax                 ; x is now end of publisher string

                lda #8 + 40 * 5
                sta $fe
                jsr printData       ; write publisher info to screen

                pla
                tax                 ; end of year string

                lda #8 + 40 * 6
                sta $fe

                ldy #$56            ; start index of year
                jsr printData       ; write year info to screen

                lda #(16 + 40 * 8) >> 8
                clc
                adc SCREEN_LOCATION
                tax
                lda #(16 + 40 * 8) & $ff
                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr setVariableWord

                lda #$08
                sta CURRENT_LINE

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

resetRoutine    inc $8005

                lda $dffd ; Identification register
                cmp #$c9
                bne noUCI

                ; Check if the UCI is busy
                lda $dffc ; Status register
                and #$30  ; State bits
                bne noUCI ; Temporarily unavailable

                lda #$84  ; Control Target (Target #4 without reply)
                sta $dffd ; Command pipe
                lda #$06  ; Command Reset Normal Cartridge
                sta $dffd ; Command pipe
                ; No params
                lda #$01  ; New Command!
                sta $dffc ; Control register

                ; Now wait patiently, note A = 01
-               bne -

noUCI           jsr turnOffCart

                cli
                jmp ($fffc)
resetRoutineEnd

screenOff       lda $d011
                and #$ef
                sta $d011

-               lda $d011
                bmi -
-               lda $d011
                bpl -
-               lda $d011
                bmi -
                rts

; first write $ff for tunes like:
; /MUSICIANS/T/Tel_Jeroen/Cybernoid.sid
; /MUSICIANS/B/Bjerregaard_Johannes/Stormlord_V2.sid
resetSID        lda #$ff
resetSIDLoop    ldx #$17
-               sta $d400,x
                dex
                bpl -
                tax
                bpl +
                lda #$08
                bpl resetSIDLoop
+
-               bit $d011
                bpl -
-               bit $d011
                bmi -
                eor #$08
                beq resetSIDLoop
                rts

initMem         lda $01
                pha

                lda #$34
                sta $01

                lda #$00
                sta $0162

                ldx #$01            ; receive DMA load
                stx $02

-               bit $0162           ; wait till DMA load is ready
                bpl -

                pla
                sta $01

                lda #$00
                sta $02             ; restore value of address $0002 to $00

                lda $0162
                cmp #$BC
                beq run_basic
                cmp #$AA
                bne go_basic
                rts
run_basic
; WE GET HERE ON INTERRUPT!
                pla                 ; 3x from the interrupt
                pla
                pla
                jsr turnOffCart
                cli                 ; we just let the interrupt occur again
                lda #1              ; disable cursor blink
                sta $CC
                lda #'R'
                sta $0200
                lda #'U'
                sta $0201
                lda #'N'
                sta $0202
                lda #0
                sta $13
                ldx #3
                jsr $AACA
                jmp $A486

go_basic
; WE GET HERE ON INTERRUPT!
                jsr turnOffCart
                jmp $ea31
initMemEnd

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
                rts

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

player          .binclude 'player/player.asm'
extraPlayer     .binclude 'player/advanced/advancedplayer.asm'
extraPlayerEnd
relocator       .binclude 'relocator.asm'
memalloc        .binclude 'memalloc.asm'
detection       .binclude 'detectionwrapper.asm'
sidFx           .binclude 'sidfx.asm'
songlengths     .binclude 'songlengths.asm'

                .enc 'screen'
screenData1     .text '  *** THE ULTIMATE C-64 SID PLAYER ***  '
                .byte $ff, $40, 40  ; line
                .byte $ff, $20, 40  ; empty line

                .text 'TITLE :'
                .byte $ff, $20, 33

                .text 'AUTHOR:'
                .byte $ff, $20, 33

                .text 'REL.BY:'
                .byte $ff, $20, 33
                .byte $00 ;end

screenData2     .text 'YEAR  :'
                .byte $ff, $20, 33
                .byte $00 ;end

screenData3     .byte $ff, $20, 40  ; empty line
                .byte $00 ;end

screenData4     .text 'SYSTEM: $D400 :'
                .byte $ff, $20, 25
                .byte $00 ;end

screenData5     .text 'SID   : $D400 :'
                .byte $ff, $20, 25
                .byte $00 ;end

screenData6     .byte $ff, $20, 40  ; empty line

                .text 'SONG  :'
                .byte $ff, $20, 33
                .byte $00 ;end

screenData7     .byte $ff, $62, 40  ; time bar
                .byte $00 ;end

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

zpAddressesUsed .null ZERO_PAGE_ADDRESSES_MAIN, relocator.ZERO_PAGE_ADDRESSES_RELOC, memalloc.ZERO_PAGE_ADDRESSES_MEMALLOC
