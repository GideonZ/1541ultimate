;-----------------------------------------------------------------------
; Ultimate SID Player V2.0 - Sidplayer for the Ultimate hardware
;
; Written by Wilfred Bos - April 2009
;                          August 2017 - January 2018
;
; Copyright (c) 2009 - 2018 Wilfred Bos / Gideon Zweijtzer
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

; TODO
; - implement reading SSL files for songlength support

; CONSTANTS
SID_MODE = $aa
DMA_MODE = $ab

OFFSET_SYSTEM_SCREEN_LOCATION = $ec       ; location at screen + $0300 -> $03ec
OFFSET_SONG_SCREEN_LOCATION = $ee         ; location at screen + $0300 -> $03ee

; ZERO PAGE ADDRESSES
CURRENT_LINE = $a0

DD00_VALUE = $a3
D018_VALUE = $a4

EXTRA_PLAYER_SIZE = $b6                   ; size of the advanced player (including song lengths data)
EXTRA_PLAYER_LOCATION = $b7               ; hi-byte of the advanced player address
PLAYER_LOCATION = $b8                     ; hi-byte of the player address
CHARROM_LOCATION = $b9                    ; hi-byte of the character ROM address where value $10 is the default CHARROM

TEMP = $bc
SID_MODEL = $bc
C64_CLOCK = $bd

SID_HEADER_LO = $fa                       ; lo-byte of sid header address
SID_HEADER_HI = $fb                       ; hi-byte of sid header address
SONG_TO_PLAY = $fc                        ; song to play where value 0 is song 1 and $ff is song 256
SCREEN_LOCATION = $fd                     ; hi-byte of screen location

                * = $8000           ; base address of cartridge

                .byte <start        ; cold start vector
                .byte >start
                .byte <startNMI     ; nmi vector
                .byte >startNMI
                .byte 'C' + $80, 'B' + $80, 'M' + $80, '8', '0'   ; CBM80

                .text 0, 'Ultimate SID Player Cartridge V2.0 - Copyright (c) 2009-2018 Wilfred Bos / Gideon Zweijtzer', 0

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

                ; do not remove this check
                jsr checkIfAsciiIsNotCorrupt

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
                lda #$04
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

checkIfAsciiIsNotCorrupt
                lda #$00
                ldx #(PETSCII - ASCII - 1) * 2
-               clc
                adc ASCII,x
                dex
                bpl -

                cmp #$d7
                beq +
                ; if a reset is performed here then the ASCII table has been changed.
                ; Check if your code editor saves ASCII characters correctly.
                jmp reset
+               rts

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

                ldx #$00
-               lda initMem,x
                sta $0100,x
                inx
                cpx #initMemEnd - initMem
                bne -

                lda $02
                cmp #SID_MODE
                beq loadSid
                jmp $0100           ; clear memory and execute DMA load, and do run

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

                ldy #$01            ; detect if header is a real SID header
                jsr readHeader
                cmp #'S'
                bne noSid
                iny
                jsr readHeader
                cmp #'I'
                bne noSid
                iny
                jsr readHeader
                cmp #'D'
                beq SidHeaderDetected
noSid           jmp reset           ; header is no real header so disable cartridge and perform a reset

SidHeaderDetected
                jsr fixHeader
                jsr moveSidHeader

                ldy #$10            ; default song offset
                jsr readHeader
                sta SONG_TO_PLAY    ; set song to play

                jsr prepareSidHeader

                jsr calculateExtraPlayerSize

                jsr calculateLocations

                jsr setupScreen

;##################################################
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
                lda EXTRA_PLAYER_LOCATION
                beq +
                sta $ab
                lda #$03
                sta $aa
                jmp runPlayer

+               lda PLAYER_LOCATION
                sta $ab
                lda #$00
                sta $aa
                jmp runPlayer

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
-               lda (SID_HEADER_LO),y
                clc
                adc #$02
                sta (SID_HEADER_LO),y
                bcc +
                iny
                lda (SID_HEADER_LO),y
                clc
                adc #$01
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
                sta SID_HEADER_LO
                lda #$03
                sta SID_HEADER_HI
noMoveNeeded    rts

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
                sta SID_HEADER_LO
                lda #$ff
                sta SID_HEADER_HI
                rts

cleanupMemory   ldy #$7f            ; clean SID header
                lda #$00
-               sta (SID_HEADER_LO),y
                dey
                bpl -

                lda #$00            ; clear temp variables
                sta CURRENT_LINE    ; $a0
                sta DD00_VALUE      ; $a3
                sta D018_VALUE      ; $a4

                sta $a6
                sta $a7
                sta $a8
                sta $a9

                sta EXTRA_PLAYER_SIZE     ; $b6
                sta EXTRA_PLAYER_LOCATION ; $b7
                sta PLAYER_LOCATION       ; $b8
                sta CHARROM_LOCATION      ; $b9

                sta TEMP            ; $bc
                sta SID_MODEL       ; $bc
                sta C64_CLOCK       ; $bd

                jsr relocator.cleanupVars
                jsr memalloc.cleanupVars

                sta $f7
                sta $f8

                sta SID_HEADER_LO   ; $fa
                sta SID_HEADER_HI   ; $fb
                sta SONG_TO_PLAY    ; $fc
                sta SCREEN_LOCATION ; $fd
                sta $fe

                lda #$20
                sta $ff
                rts

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

noAreaPossible  jsr memalloc.calcSmallestPlayerLocation
                lda #$00
                sta EXTRA_PLAYER_LOCATION
                rts

calcLocBasedOnFreePages
                jsr memalloc.calcPlayerScreenLocForRelocArea
                jsr memalloc.calcExtraPlayerLocForRelocArea
                rts

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

                lda SCREEN_LOCATION
                clc
                adc #$03
                ldy extraPlayer.clockLoc
                ldx extraPlayer.clockLoc + 1
                jsr setValue

                ldy extraPlayer.songLenLoc1
                ldx extraPlayer.songLenLoc1 + 1
                jsr setValue

                ldy #OFFSET_SONG_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldy extraPlayer.songNumLoc
                ldx extraPlayer.songNumLoc + 1
                jsr setValue
                pla
                iny
                jsr writeAddress

                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldy extraPlayer.sidModelLoc
                ldx extraPlayer.sidModelLoc + 1
                jsr setValue
                pla
                iny
                jsr writeAddress

                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr getVariableWord
                pha
                txa
                ldy extraPlayer.c64ModelLoc
                ldx extraPlayer.c64ModelLoc + 1
                jsr setValue
                pla
                iny
                jsr writeAddress

                lda player.offSongNr + 1
                clc
                adc PLAYER_LOCATION
                pha
                lda player.offSongNr
                ldy extraPlayer.songNumSet
                ldx extraPlayer.songNumSet + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                lda SCREEN_LOCATION
                clc
                adc #$03
                pha
                lda #$80
                ldy extraPlayer.spriteLoc
                ldx extraPlayer.spriteLoc + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                lda player.offPlayLoop + 1
                clc
                adc PLAYER_LOCATION
                pha
                lda player.offPlayLoop
                ldy extraPlayer.playerLoopLoc
                ldx extraPlayer.playerLoopLoc + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                lda player.offExtraPlay1 + 1
                clc
                adc PLAYER_LOCATION
                pha
                lda player.offExtraPlay1
                ldy extraPlayer.epCallLoc
                ldx extraPlayer.epCallLoc + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                lda player.offFastForw + 1
                clc
                adc PLAYER_LOCATION
                pha
                lda player.offFastForw
                ldy extraPlayer.fastFwd
                ldx extraPlayer.fastFwd + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                lda player.offSpeed1 + 1
                clc
                adc PLAYER_LOCATION
                pha
                lda player.offSpeed1
                ldy extraPlayer.hdrSpeedFlag1
                ldx extraPlayer.hdrSpeedFlag1 + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                lda player.offSpeed2 + 1
                clc
                adc PLAYER_LOCATION
                pha
                lda player.offSpeed2
                ldy extraPlayer.hdrSpeedFlag2
                ldx extraPlayer.hdrSpeedFlag2 + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                lda player.offSpeed3 + 1
                clc
                adc PLAYER_LOCATION
                pha
                lda player.offSpeed3
                ldy extraPlayer.hdrSpeedFlag3
                ldx extraPlayer.hdrSpeedFlag3 + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                ldy #$12            ; byte 4 of speed flags
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

                lda PLAYER_LOCATION
                pha
                lda #$00
                ldy extraPlayer.playerLoc
                ldx extraPlayer.playerLoc + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

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
                ldy extraPlayer.c64ModelFlag
                ldx extraPlayer.c64ModelFlag + 1
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

getVariableByte lda #$00
                sta $b0
                lda SCREEN_LOCATION
                clc
                adc #$03
                sta $b1

                jmp readScreen

getVariableWord jsr getVariableByte
                tax
                iny
                jmp readScreen

setVariableByte pha
                lda #$00
                sta $b0
                lda SCREEN_LOCATION
                clc
                adc #$03
                sta $b1
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
                ldy #$0b            ; get init address hi-byte (note that SID header is converted to little endian here)
                jsr readHeader
                pha
                ldy #$0a            ; get init address lo-byte (note that SID header is converted to little endian here)
                jsr readHeader
                ldy player.offInit
                ldx player.offInit + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                ; handle play address
                ldy #$0d            ; get play address hi-byte (note that SID header is converted to little endian here)
                jsr readHeader
                pha
                ldy #$0c            ; get play address lo-byte (note that SID header is converted to little endian here)
                jsr readHeader
                ldy player.offPlay
                ldx player.offPlay + 1
                jsr setValue
                iny
                pla
                jsr writeAddress

                ldy player.offReloc1
                ldx player.offReloc1 + 1
                jsr relocator.relocByte

                ldy player.offCiaLU1
                ldx player.offCiaLU1 + 1
                jsr relocator.relocByte

                ldy player.offCiaLU2
                ldx player.offCiaLU2 + 1
                jsr relocator.relocByte

                ldy player.offCiaFix
                ldx player.offCiaFix + 1
                jsr relocator.relocByte

                lda relocator.BASE_ADDRESS
                ldy player.offBasicEnd
                ldx player.offBasicEnd + 1
                jsr setValue

                lda relocator.BASE_ADDRESS
                ldy player.offHiPlayer
                ldx player.offHiPlayer + 1
                jsr setValue

                ldy player.offCiaFix1
                ldx player.offCiaFix1 + 1
                jsr relocator.relocByte

                ldy player.offCiaFix2
                ldx player.offCiaFix2 + 1
                jsr relocator.relocByte

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
                iny
                lda #$a7
                jsr writeAddress

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

                ldy #$0b            ; get init address hi-byte
                jsr readHeader
                jsr getBankInit
                ldy player.offInitBank
                ldx player.offInitBank + 1
                jsr setValue

                ldy #$0d            ; get play address hi-byte
                jsr readHeader
                jsr getBankPlay
                ldy player.offPlayBank
                ldx player.offPlayBank + 1
                jsr setValue

                ldy #$0c            ; get play address lo-byte
                jsr readHeader
                bne playNotZero
                iny
                jsr readHeader
                bne playNotZero     ; is play address zero?

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
                iny                 ; write another NOP
                jsr writeAddress
                jmp continueInitPlayer

playNotZero     lda #$85            ; STA $01
                ldy player.offInitAfter
                ldx player.offInitAfter + 1
                jsr setValue
                iny
                lda #$01
                jsr writeAddress

continueInitPlayer
                lda D018_VALUE
                ldy player.offD018
                ldx player.offD018 + 1
                jsr setValue

                lda DD00_VALUE
                ldy player.offDD00
                ldx player.offDD00 + 1
                jsr setValue

                ldy player.offHiIrq
                ldx player.offHiIrq + 1
                jsr relocator.relocByte

                ldy player.offHiBrk
                ldx player.offHiBrk + 1
                jsr relocator.relocByte

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

                ; speed flags
                lda SONG_TO_PLAY    ; read current song
                jsr calcSpeedFlag
                jsr setSpeedFlags

                cmp #$01            ; do not use extra player when speed flag is 1
                beq noExtraPlayWithFF

                lda EXTRA_PLAYER_LOCATION
                beq noExtraPlayer

                ; do not use extra player when play address is zero
                ldy #$0c            ; get play address
                jsr readHeader
                bne +
                iny
                jsr readHeader
                beq noExtraPlayWithFF   ; is play address zero?
+
                lda EXTRA_PLAYER_LOCATION
                pha

                lda #$20            ; JSR
                ldy player.offExtraPlay1
                ldx player.offExtraPlay1 + 1
                jsr setValue
                iny
                lda #$00            ; lo-byte of extra player
                jsr writeAddress
                iny
                pla
                jsr writeAddress

extraPLayForFF
                lda EXTRA_PLAYER_LOCATION
                pha

                lda #$20            ; JSP
                ldy player.offExtraPlay2
                ldx player.offExtraPlay2 + 1
                jsr setValue
                iny
                lda #$00            ; lo-byte of extra player
                jsr writeAddress
                iny
                pla
                jsr writeAddress
                rts

noExtraPlayer   lda #$ea            ; NOP
                ldy player.offExtraPlay1
                ldx player.offExtraPlay1 + 1
                jsr setValue
                iny
                jsr writeAddress
                iny
                jsr writeAddress

                ldy player.offExtraPlay2
                ldx player.offExtraPlay2 + 1
                jsr setValue
                iny
                jsr writeAddress
                iny
                jsr writeAddress
                rts

noExtraPlayWithFF
                lda EXTRA_PLAYER_LOCATION
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
                iny
                jsr writeAddress
                iny
                jsr writeAddress
                jmp extraPLayForFF

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

copyPlayer      lda #$00
                sta $ac

                lda PLAYER_LOCATION
                sta $ad

                lda #>player
                sta $ab

                lda #<player
                clc
                adc player.headerSize
                sta $aa
                bcc +
                inc $ab
+
                ldx #$02      ; size player in blocks of $0100
                ldy #$00
-               lda ($aa),y
                sta ($ac),y
                iny
                bne -
                inc $ab
                inc $ad
                dex
                bne -
                rts

copyExtraPlayer lda #$00
                sta $ac

                lda EXTRA_PLAYER_LOCATION
                sta $ad

                lda #>extraPlayer
                sta $ab

                lda #<extraPlayer
                clc
                adc extraPlayer.headerSize
                sta $aa
                bcc +
                inc $ab
+
                ldx #(extraPlayerEnd - extraPlayer) / 256 + 1      ; size player in blocks of $0100
                ldy #$00
-               lda ($aa),y
                sta ($ac),y
                iny
                bne -
                inc $ab
                inc $ad
                dex
                bne -
                rts

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
                jsr readHeader     ; get load address low at offset $7c
                ldy #$08            ; set load address low
                sta (SID_HEADER_LO),y
                inx
                txa
                tay
                jsr readHeader     ; get load address high at offset $7d
                ldy #$09            ; set load address high
                sta (SID_HEADER_LO),y
+
                ldy #$0a            ; get init address
                jsr readHeader
                bne +
                iny
                jsr readHeader
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
                ldy #$0c            ; get play address low
                jsr readHeader
                sta $aa
                ldy #$0a            ; compare with init low
                jsr readHeader
                cmp $aa
                bne +
                ldy #$0d            ; get play address high
                jsr readHeader
                sta $aa
                ldy #$0b            ; compare with init high
                jsr readHeader
                cmp $aa
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
                pla
                sta $ab
                pla
                sta $aa

                ldx #$00
-               lda runRoutine,x
                sta $0100,x
                inx
                cpx #runRoutineEnd - runRoutine
                bne -

                jmp $0100

copyChars       lda CHARROM_LOCATION
                sta $ff           ; charset destination address
                cmp #$10
                beq dontCopyChar

                lda #$d0
                sta $f8

                ldy #$00
                sty $fe
                sty $f7

                ldx #$00
-               lda CharROMCopy,x
                sta $0100,x
                inx
                cpx #CharROMCopyEnd - CharROMCopy
                bne -

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

                ldx #$00
-               lda screenWriteBegin,x
                sta $0100,x
                inx
                cpx #(screenWriteEnd - screenWriteBegin) + 1
                bne -

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
                sta $fe
                lda #>screenData1
                sta $ff

                jsr writeScreenData

                jsr canReleaseFieldSplit
                cpy #$00
                beq +

                ; write year label
                lda #<screenData2
                sta $fe
                lda #>screenData2
                sta $ff

                jsr writeScreenData
+
                ; write empty line
                lda #<screenData3
                sta $fe
                lda #>screenData3
                sta $ff

                jsr writeScreenData

                ; write system label
                lda #<screenData4
                sta $fe
                lda #>screenData4
                sta $ff

                jsr writeScreenData

                ; write SID label
                lda #<screenData5
                sta $fe
                lda #>screenData5
                sta $ff

                jsr writeScreenData

                ldy #$7a            ; is second SID address defined?
                jsr readHeader
                beq noMoreSids

                lda #1
                jsr writeSidChipCount

                ; write SID label
                lda #<screenData5
                sta $fe
                lda #>screenData5
                sta $ff

                jsr writeScreenData
                lda #2
                jsr writeSidChipCount

                ldy #$7b            ; is third SID address defined?
                jsr readHeader
                beq noMoreSids

                ; write SID label
                lda #<screenData5
                sta $fe
                lda #>screenData5
                sta $ff

                jsr writeScreenData
                lda #3
                jsr writeSidChipCount
noMoreSids
                ; write SONG label
                lda #<screenData6
                sta $fe
                lda #>screenData6
                sta $ff

                jsr writeScreenData

                lda #(40 * 23) >> 8
                clc
                adc SCREEN_LOCATION
                sta $f8
                lda #(40 * 23) & $ff
                sta $f7

                ; write time bar
                lda #<screenData7
                sta $fe
                lda #>screenData7
                sta $ff

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

printSidInfo
                lda $f7             ; restore sid header address
                sta SID_HEADER_LO

                lda $f8
                sta SID_HEADER_HI

                jsr setCurrentLinePosition

                jsr detection.detectSystem
                sta C64_CLOCK
                sty SID_MODEL
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

                ldy #$7a            ; is second SID address defined?
                jsr readHeader
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
                lda $fe
                ldy #OFFSET_SONG_SCREEN_LOCATION
                jsr setVariableWord

                ldx #'0'
                lda SONG_TO_PLAY
                pha

                cmp #$ff
                bne skipFirstNibble
                jsr write256

                inc $fe
                bne writeCurrentSongDone

skipFirstNibble pla
                jsr printNibble

writeCurrentSongDone
                inc $fe
                lda #$2f
                ldy #$00
                jsr screenWrite
                inc $fe
                inc $fe

                ldy #$0f
                jsr readHeader
                and #$01
                beq songs255
                jsr write256
                jmp ScreenDone

songs255        ldy #$0e
                jsr readHeader
                tax
                dex
                txa
                jsr printNibble
ScreenDone      pla

                ; set sprite pointer
                lda #$80
                sta $aa
                lda SCREEN_LOCATION
                clc
                adc #$03
                sta $ab
                ldx #6
-               lda $ab
                lsr
                sta $ab
                lda $aa
                ror
                sta $aa
                dex
                bne -
                pha

                lda #$00
                sta $aa
                lda SCREEN_LOCATION
                clc
                adc #$03
                sta $ab
                ldy #$f8
                pla
                jsr writeScreen
                rts

write256        ldy #$00
                lda #'2'
                sta ($fe),y
                inc $fe
                lda #'5'
                sta ($fe),y
                inc $fe
                lda #'6'
                sta ($fe),y
                rts

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
                jmp getBank

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
                tya
                pha                 ; save index

                ldy #$00
charConvLoop    lda ($a9),y
                beq conversionEnd
                cmp $a6
                bne checkNextChar

                lda ($a7),y
                sta $a6
                bne conversionEnd

checkNextChar   iny
                bne charConvLoop

conversionEnd   pla
                tay
                lda $a6

                pha
                and #$40
                beq noConversion
                pla
                and #$9f
                pha
noConversion    pla
                and #$7f            ; only first 128 chars allowed, so map last 128 to first 128 chars
                jsr screenWrite
                iny
                dex
                bne fillData
stopPrintData
                lda $ac
                sta SID_HEADER_LO
                rts

printNibble     tax
                inx
countOne
                sed
                ldy #$00
                lda #$00
convertToDec    clc
                adc #$01
                bcc noIncHundred
                iny
noIncHundred
                dex
                bne convertToDec
stopDecConversion
                cld

                ldx #$00
                cpy #$00
                beq skipFirstNibble2
                pha
                tya
                clc
                adc #$30
                ldy #$00
                jsr screenWrite       ; print hundred
                inx
                inc $fe
                pla
skipFirstNibble2

                pha
                lsr
                lsr
                lsr
                lsr
                cpx #$00
                bne forcePrint1
                cmp #$00
                beq skipSecondNibble

forcePrint1     and #$0f
                clc
                adc #$30
                jsr screenWrite
                inc $fe

skipSecondNibble
                pla
                and #$0f
                clc
                adc #$30
                jsr screenWrite
                inc $fe
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

reset           ldx #$00
-               lda resetRoutine,x
                sta $0100,x
                inx
                cpx #resetRoutineEnd - resetRoutine
                bne -
                jmp $0100

resetRoutine    lda #40
                sta $dfff
                inc $8005
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
                lda #$40
                sta $dfff           ; turn off cartridge
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
                lda #$40
                sta $dfff           ; turn off cartridge
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
                sta $dfff
                jmp ($00aa)
runRoutineEnd

writeScreenData
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
                jsr screenWrite           ; write to screen
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

writeData       jsr screenWrite           ; write to screen

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
                lda #$00
                sta $ff

                ; multiply by 40
                ldx $ff
                lda $fe
                asl
                rol $ff
                asl
                rol $ff

                clc
                adc $fe
                sta $fe
                txa
                adc $ff
                sta $ff

                asl $fe
                rol $ff

                asl $fe
                rol $ff
                asl $fe
                rol $ff
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

                ldy #$7a            ; is second SID address defined?
                jsr readHeader
                jsr printHex
                jmp checkVersion

+               cmp #$03
                bne checkVersion

                lda $fe
                clc
                adc #10
                sta $fe

                ; print SID address for second SID
                ldy #$7b            ; is second SID address defined?
                jsr readHeader
                jsr printHex

checkVersion    pla                 ; check if system info needs to be printed
                pha
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

                ; print '6581 / 8580'
                lda #<S65818580Lbl
                sta $aa
                lda #>S65818580Lbl
                sta $ab
                jmp printModel

print6581       lda #<S6581Lbl
                sta $aa
                lda #>S6581Lbl
                sta $ab
                jmp printModel

print8580       lda #<S8580Lbl
                sta $aa
                lda #>S8580Lbl
                sta $ab
                jmp printModel

printUnknownModel
                lda #<SUnknownLbl
                sta $aa
                lda #>SUnknownLbl
                sta $ab

printModel      jsr setCurrentLinePosition
                lda $fe
                clc
                adc #16
                sta $fe
                bcc +
                inc $ff
+
                ldy #$00
-               lda ($aa),y
                cmp #$00
                beq +
                jsr $0100           ; write to screen
                iny
                bne -
+
                iny
                sty $ac

                ; print Clock info
                pla                 ; check if system info needs to be printed
                bne checkSidHeader2
                ; print system info
                lda C64_CLOCK
                beq printPal
                cmp #$03
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

                ; print 'PAL / NTSC'
                lda #<PALNTSCLbl
                sta $aa
                lda #>PALNTSCLbl
                sta $ab
                jmp printClock

printPal        lda #<PALLbl
                sta $aa
                lda #>PALLbl
                sta $ab
                jmp printClock

printNtsc       lda #<NTSCLbl
                sta $aa
                lda #>NTSCLbl
                sta $ab
                jmp printClock

printUnknownClock
                lda #<UnknownLbl
                sta $aa
                lda #>UnknownLbl
                sta $ab

printClock      lda $fe
                clc
                adc $ac
                sta $fe
                bcc +
                inc $ff
+
                ldy #$00
-               lda ($aa),y
                cmp #$00
                beq +
                jsr $0100           ; write to screen
                iny
                bne -
+
                inc CURRENT_LINE
                jsr setCurrentLinePosition
                rts

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
detection       .binclude 'detection.asm'
songlengths     .binclude 'songlengths.asm'

                .enc 'screen'
screenData1     .text ' **** THE ULTIMATE C-64 SID PLAYER **** '

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

screenData7     .text '                                   05:00'
                .byte $ff, $62, 40  ; time bar
                .byte $00 ;end

PALLbl          .text '/ PAL', 0
NTSCLbl         .text '/ NTSC', 0
PALNTSCLbl      .text '/ PAL / NTSC', 0
UnknownLbl      .text '/ UNKOWN', 0
S6581Lbl        .text '6581', 0
S8580Lbl        .text '8580', 0
S65818580Lbl    .text '6581 / 8580', 0
SUnknownLbl     .text 'UNKOWN', 0
                .enc 'none'

ASCII             .byte $c0, $c1, $c2, $c3, $c4, $c5, $c6             ; all A variants
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
                  .byte 0
                  ;     'ÀÁÂÃÄÅÆàáâãäåæÈÉÊËèéêëÌÍÎÏìíîïÒÓÔÕÖØòóôõöøÙÚÛÜùúûüÇçÑñÝŸýÿß'
PETSCII           .null 'AAAAAAAaaaaaaaEEEEeeeeIIIIiiiiOOOOOOooooooUUUUuuuuCcNnYYyyB'

                  ;examples special characters:
                  ; /HVSC/C64Music/MUSICIANS/W/Walt/Maelkeboetten.sid (Mæclkebøtten)
                  ; /HVSC/C64Music/MUSICIANS/A/Ass_It/Lasst_Uns_Froh.sid (Laßt Uns Froh)
                  ; /HVSC/C64Music/GAMES/A-F/Captain_Blood.sid (by François Lionet)
                  ; /HVSC/C64Music/MUSICIANS/D/Da_Blondie/Brain_Damage.sid (by Attila Szõke)
                  ; etc.
