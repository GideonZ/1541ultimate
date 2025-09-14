;-----------------------------------------------------------------------
; Ultimate MUS Player V1.0b - Sidplayer for the Ultimate hardware
;
; Written by Wilfred Bos - April 2019
;
; Copyright (c) 2019-2023 Wilfred Bos / Gideon Zweijtzer
;
; Info:
;  Editing the source code should be done in a text editor which supports UTF-8.
;
;  The MUS cart acts like it is a SID tune with all the info in the SID header
;  and the Sidplayer64 should already be installed in memory.
;
;  Call the player by activating the cartridge and performing a reset.
;  The player needs a SID header which is located somewhere in memory. The SID
;  header can be located at any address. The location of the header should
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

CURRENT_LINE_CALC_LO =  $0ffb
CURRENT_LINE_CALC_HI =  $0ffc

MUS_NUMBER_OF_LINES = $0ffd
MUS_INFO_LOCATION_LO = $b0
MUS_INFO_LOCATION_HI = $b1

MUS_INFO_CONV_LOC_LO = $f8
MUS_INFO_CONV_LOC_HI = $f9

MUS_INFO_SCREEN_LO = $a8
MUS_INFO_SCREEN_HI = $a9

MUS_INFO_COLOR_LO = $a8
MUS_INFO_COLOR_HI = $a9

MUS_CURRENT_COLOR = $0ff9

MUS_INFO_INVERSE = $0ffa

ZERO_PAGE_ADDRESSES_MUS = [
    MUS_INFO_CONV_LOC_LO,
    MUS_INFO_CONV_LOC_HI,

    MUS_INFO_COLOR_LO,
    MUS_INFO_COLOR_HI
]

                * = $8000           ; base address of cartridge

                .byte <baseCrt.start        ; cold start vector
                .byte >baseCrt.start
                .byte <baseCrt.startNMI     ; nmi vector
                .byte >baseCrt.startNMI
                .byte 'C' + $80, 'B' + $80, 'M' + $80, '8', '0'   ; CBM80

                .text 0, 'Ultimate MUS Player Cartridge V1.0b - Copyright (c) 2019-2023 Wilfred Bos / Gideon Zweijtzer', 0

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
                and #$f0
                cmp #$d0
                bne +
                ; don't allow screen updates when the screen is located at $Dxxx and therefore the extra player should not be activated
                lda #$00
                sta EXTRA_PLAYER_LOCATION
+
                jsr setupScreen
                jsr writeScreenLabels
                jsr printMusInfo

                jsr songlengths.displayCurSongLength

                jsr copyPlayer
                jsr setPlayerVars
                jsr setMusPlayerVars

                lda EXTRA_PLAYER_LOCATION
                beq +
                jsr copyExtraPlayer
                jsr relocateExtraPlayer
                jsr setExtraPlayerVars
                jsr setMusExtraPlayerVars

                lda #$03
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
                lda #$c0
                sta EXTRA_PLAYER_LOCATION
                lda #$08
                sta PLAYER_LOCATION
                lda #$04
                sta SCREEN_LOCATION
                lda #$10
                sta CHARROM_LOCATION

handleMusInfo   lda $1001
                clc
                adc $1003
                clc
                adc $1005
                clc
                adc #$10
                sta MUS_INFO_LOCATION_HI

                lda $1000
                clc
                adc $1002
                bcc +
                inc MUS_INFO_LOCATION_HI
+               clc
                adc $1004
                bcc +
                inc MUS_INFO_LOCATION_HI
+               clc
                adc #$06
                bcc +
                inc MUS_INFO_LOCATION_HI
+               sta MUS_INFO_LOCATION_LO

                lda MUS_INFO_LOCATION_LO
                sta $0ffe
                lda MUS_INFO_LOCATION_HI
                sta $0fff

convertMusInfo  ldx #$00
                txa
-               sta $0a00,x
                sta $0b00,x
                sta $0c00,x
                sta $0d00,x
                sta $0e00,x
                inx
                bne -

                lda #$01
                sta MUS_NUMBER_OF_LINES

                lda #$0f
                sta MUS_CURRENT_COLOR

                lda #$00
                sta MUS_INFO_CONV_LOC_LO
                lda #$80
                sta MUS_INFO_COLOR_LO

                lda #$0a
                sta MUS_INFO_CONV_LOC_HI
                sta MUS_INFO_COLOR_HI

                lda #$00
                sta MUS_INFO_INVERSE

                ldy #$00
infoLoop        lda (MUS_INFO_LOCATION_LO),y
                bne +
                jmp infoEnd

+               jsr handleColorChar
                beq nextMusChar                 ; if it is a color, then jump to next char

                cmp #$0a
                beq nextMusChar

                jsr handleNextLine
                beq nextMusChar

                ldx MUS_NUMBER_OF_LINES
                cpx #$06
                bne +
                ldx #$05                        ; maximum number of lines is 5
                stx MUS_NUMBER_OF_LINES
                jmp infoEnd

+               cmp #$92                        ; inverse OFF command
                bne +
                lda #$00
                sta MUS_INFO_INVERSE
                jmp nextMusChar

+               cmp #$12                        ; inverse ON command
                bne +
                lda #$80
                sta MUS_INFO_INVERSE
                jmp nextMusChar

+               jsr handleCursorLeft
                beq nextMusChar

                cmp #$20
                bcc nextMusChar                 ; Unknown command when char is lower than $20 so skip it

+               cmp #$40
                beq +
                bcc ++
                cmp #$60
                bcs ++
                ; char is >= $40 and < $60
+               sec
                sbc #$40
                jmp storeChar

+               cmp #$60
                beq +
                bcc ++
                cmp #$80
                bcs ++
                ; char is >= $60 and < $80
+               clc
                adc #$60
                jmp storeChar

+               cmp #$80
                beq +
                bcc ++
                cmp #$c0
                bcs ++
                ; char is >= $80 and < $c0
+               sec
                sbc #$40
+
storeChar       and #$7f
                ora MUS_INFO_INVERSE
                sta (MUS_INFO_CONV_LOC_LO),y    ; store char

                lda MUS_CURRENT_COLOR
                sta (MUS_INFO_COLOR_LO),y       ; store color

                inc MUS_INFO_CONV_LOC_LO
                inc MUS_INFO_COLOR_LO

nextMusChar     inc MUS_INFO_LOCATION_LO
                bne +
                inc MUS_INFO_LOCATION_HI
+               jmp infoLoop
infoEnd         jmp cleanupMusInfo

handleCursorLeft
                cmp #$14
                beq doLeft
                cmp #$9d                        ; cursor LEFT command
                bne ++
doLeft          lda MUS_INFO_CONV_LOC_LO
                beq +                           ; don't do cursor left when at the beginning of the line
                dec MUS_INFO_CONV_LOC_LO
                dec MUS_INFO_COLOR_LO
+               lda #$00
+               rts

printMusInfo    ldy #$05
                lda #$07
-               sta $d878,y
                dey
                bpl -
                lda #$0f
                sta $d87e

                lda #$00
                sta MUS_INFO_CONV_LOC_LO

                lda #$80
                sta MUS_INFO_SCREEN_LO
                lda #$04
                sta MUS_INFO_SCREEN_HI

                ldx MUS_NUMBER_OF_LINES
                beq stopPrintMus
-               ldy #$00
                lda (MUS_INFO_CONV_LOC_LO),y
                beq stopPrintMus
-               lda (MUS_INFO_CONV_LOC_LO),y
                sta (MUS_INFO_SCREEN_LO),y

                lda MUS_INFO_SCREEN_HI
                eor #$dc                        ; toggle to color screen or back to char screen
                sta MUS_INFO_SCREEN_HI
                lda MUS_INFO_CONV_LOC_LO
                eor #$80                        ; toggle to color info or back to char info
                sta MUS_INFO_CONV_LOC_LO
                bmi -

                iny
                cpy #$20
                bne -

                lda MUS_INFO_SCREEN_LO
                clc
                adc #$28
                sta MUS_INFO_SCREEN_LO
                bcc +
                inc MUS_INFO_SCREEN_HI
+
                inc MUS_INFO_CONV_LOC_HI
                dex
                bne --
stopPrintMus    rts

handleNextLine  cmp #$0d                        ; next line
                bne +

                lda #$20                        ; store dummy space to indicate that line is used
                sta (MUS_INFO_CONV_LOC_LO),y    ; store char
                lda MUS_CURRENT_COLOR
                sta (MUS_INFO_COLOR_LO),y       ; store color

                inc MUS_NUMBER_OF_LINES

                inc MUS_INFO_CONV_LOC_HI
                inc MUS_INFO_COLOR_HI

                lda #$80
                sta MUS_INFO_COLOR_LO
                lda #$00
                sta MUS_INFO_CONV_LOC_LO

                lda #$00
                sta MUS_INFO_INVERSE
+               rts

handleColorChar
                ldx #$00
-               cmp colorCodes,x
                beq colorCodeFound
                inx
                cpx #16
                bne -
                cpx #00
                rts

colorCodeFound  stx MUS_CURRENT_COLOR
                rts

cleanupMusInfo  ldx MUS_NUMBER_OF_LINES
                dex
                txa
                clc
                adc #$0a
                sta MUS_INFO_CONV_LOC_HI

                lda #$00
                sta MUS_INFO_CONV_LOC_LO

-               ldy #$00
-               lda (MUS_INFO_CONV_LOC_LO),y
                beq +
                cmp #$20
                beq +
                jmp notEmpty
+               iny
                cpy #$20
                bne -

                ; last line is empty so skip it
                dec MUS_NUMBER_OF_LINES

                dec MUS_INFO_CONV_LOC_HI
                lda MUS_INFO_CONV_LOC_HI
                cmp #$09
                bne --
notEmpty
                lda MUS_NUMBER_OF_LINES
                beq notEmpty2

                lda #$0a
                sta MUS_INFO_CONV_LOC_HI

-               ldy #$00
-               lda (MUS_INFO_CONV_LOC_LO),y
                beq +
                cmp #$20
                beq +
                jmp notEmpty2
+               iny
                cpy #$20
                bne -

                ; first line is empty so skip it
                dec MUS_NUMBER_OF_LINES
                inc MUS_INFO_CONV_LOC_HI
                lda MUS_NUMBER_OF_LINES
                beq notEmpty2
                lda MUS_INFO_CONV_LOC_HI
                cmp #$0f
                bne --
notEmpty2       rts

setMusExtraPlayerVars
                lda #$00                ; disable mus for the extra player since colors are handled by the crt
                ldy extraPlayer.musTuneFlag
                ldx extraPlayer.musTuneFlag + 1
                jmp setValue

setMusPlayerVars
                lda #20 - 1
                sec
                sbc MUS_NUMBER_OF_LINES
                ldy player.offNumLines
                ldx player.offNumLines + 1
                jmp setValue

writeScreenLabels
                lda SCREEN_LOCATION
                pha

                lda #<screenData1
                ldy #>screenData1
                jsr writeScreenData

                ldx MUS_NUMBER_OF_LINES
                beq skipInfoDisplay
                lda #<screenData8
                ldy #>screenData8
                jsr writeScreenData

                ldx MUS_NUMBER_OF_LINES
-               txa
                pha
                ; write empty line
                lda #<screenData3
                ldy #>screenData3
                jsr writeScreenData
                pla
                tax
                dex
                bne -

skipInfoDisplay lda #<screenData2
                ldy #>screenData2
                jsr writeScreenData

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

                lda #$03
                ldx MUS_NUMBER_OF_LINES
                beq +
                clc
                adc #$01
+               clc
                adc MUS_NUMBER_OF_LINES
                sta CURRENT_LINE

                lda #(16 + 40 * 5)
                ldy MUS_NUMBER_OF_LINES
                beq +
-               clc
                adc #40
+               sta CURRENT_LINE_CALC_LO
                bcc +
                inc CURRENT_LINE_CALC_HI
+               dey
                bpl -

                lda CURRENT_LINE_CALC_HI
                clc
                adc SCREEN_LOCATION
                tax
                lda CURRENT_LINE_CALC_LO
                ldy #OFFSET_SYSTEM_SCREEN_LOCATION
                jsr setVariableWord

                jsr setCurrentLinePosition

                lda $fe
                clc
                adc #$08
                sta $fe

                ldy #$16            ; offset title in SID header
                ldx #32             ; print max 32 chars
                jsr printData       ; write title data to screen

                inc CURRENT_LINE
                inc CURRENT_LINE

                jmp printSidInfo

                .include 'sidcommon.asm'

player          .binclude 'player/player.asm'
extraPlayer     .binclude 'player/advanced/advancedplayer.asm'
extraPlayerEnd
relocator       .binclude 'relocator.asm'
detection       .binclude 'detectionwrapper.asm'
sidFx           .binclude 'sidfx.asm'
songlengths     .binclude 'songlengths.asm'

colorCodes      .byte $90, $05, $1c, $9f, $9c, $1e, $1f, $9e, $81, $95, $96, $97, $98, $99, $9a, $9b;

                .enc 'screen'
screenData1     .text '  *** THE C-64 ULTIMATE MUS PLAYER ***  '
                .byte $ff, $40, 40  ; line
                .byte $ff, $20, 40  ; empty line
                .byte $00 ;end

screenData8     .text 'INFO  :'
                .byte $ff, $20, 33
                .byte $00 ;end

screenData3     .byte $ff, $20, 40  ; empty line
                .byte $00 ;end

screenData2     .text 'TITLE :'
                .byte $ff, $20, 33
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
                .enc 'none'

zpAddressesUsed .null ZERO_PAGE_ADDRESSES_MAIN, ZERO_PAGE_ADDRESSES_MUS, relocator.ZERO_PAGE_ADDRESSES_RELOC
