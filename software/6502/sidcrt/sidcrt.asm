;-----------------------------------------------------------------------
; Ultimate SID Player V2.0d - Sidplayer for the Ultimate hardware
;
; Written by Wilfred Bos - April 2009
;                          August 2017 - January 2018
;
; Copyright (c) 2009 - 2023 Wilfred Bos / Gideon Zweijtzer
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

                * = $8000           ; base address of cartridge

                .byte <baseCrt.start        ; cold start vector
                .byte >baseCrt.start
                .byte <baseCrt.startNMI     ; nmi vector
                .byte >baseCrt.startNMI
                .byte 'C' + $80, 'B' + $80, 'M' + $80, '8', '0'   ; CBM80

                .text 0, 'Ultimate SID Player Cartridge V2.0d - Copyright (c) 2009-2023 Wilfred Bos / Gideon Zweijtzer', 0

baseCrt         .binclude 'basecrt.asm'

                ldx #$00
-               lda readMem,x
                sta $0180,x
                inx
                cpx #readMemEnd - readMem
                bne -

                ldy #$03
-               jsr readHeader
                cmp SIDMagic - 1,y ; detect if header is a real SID header
                bne baseCrt.reset  ; header is no real header so perform a reset
                dey
                bne -

                jsr prepareSidHeader

                ldy #$10            ; default song offset
                jsr readHeader
                sta SONG_TO_PLAY    ; set song to play

                jsr calculateExtraPlayerSize
                jsr calculateLocations

                lda SCREEN_LOCATION
                beq noScreen
                and #$f0
                cmp #$d0
                bne +
                ; don't allow screen updates when the screen is located at $Dxxx and therefore the extra player should not be activated
                lda #$00
                sta EXTRA_PLAYER_LOCATION
+
                jsr setupScreen
                jsr writeScreenLabels

                jsr songlengths.displayCurSongLength

noScreen        jsr copyPlayer
                jsr setPlayerVars

                lda EXTRA_PLAYER_LOCATION
                beq +

                jsr copyExtraPlayer
                jsr relocateExtraPlayer
                jsr setExtraPlayerVars

                lda #$05
                sta songlengths.DEFAULT_SONG_LENGTH

                jsr setupSldb
+
                lda PLAYER_LOCATION
                sta $ab
                lda #$00
                sta $aa
                jmp runPlayer

;##################################################

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

writeScreenLabels
                lda SCREEN_LOCATION
                pha

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
                jmp printSidInfo

                .include 'sidcommon.asm'

player          .binclude 'player/player.asm'
extraPlayer     .binclude 'player/advanced/advancedplayer.asm'
extraPlayerEnd
relocator       .binclude 'relocator.asm'
memalloc        .binclude 'memalloc.asm'
detection       .binclude 'detectionwrapper.asm'
sidFx           .binclude 'sidfx.asm'
songlengths     .binclude 'songlengths.asm'

                .enc 'screen'
screenData1     .text '  *** THE C-64 ULTIMATE SID PLAYER ***  '
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

zpAddressesUsed .null ZERO_PAGE_ADDRESSES_MAIN, relocator.ZERO_PAGE_ADDRESSES_RELOC, memalloc.ZERO_PAGE_ADDRESSES_MEMALLOC
